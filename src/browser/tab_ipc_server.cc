#include "browser/tab_ipc_server.h"

#include <cerrno>
#include <cstdio>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/base/cef_logging.h"
#include "include/cef_waitable_event.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

#include "browser/tab_manager.h"

namespace rethread {
namespace {
TabIpcServer* g_ipc_server = nullptr;

template <typename Func>
auto RunOnUiAndWait(Func&& func) -> decltype(func()) {
  using ReturnType = decltype(func());
  if (CefCurrentlyOn(TID_UI)) {
    return func();
  }

  std::function<ReturnType()> bound = std::forward<Func>(func);
  CefRefPtr<CefWaitableEvent> event =
      CefWaitableEvent::CreateWaitableEvent(false, false);
  auto result_holder = std::make_shared<std::optional<ReturnType>>();

  CefPostTask(
      TID_UI,
      base::BindOnce(
          [](std::shared_ptr<std::optional<ReturnType>> holder,
             CefRefPtr<CefWaitableEvent> event,
             std::function<ReturnType()> task) {
            holder->emplace(task());
            event->Signal();
          },
          result_holder, event, bound));

  event->Wait();
  return std::move(result_holder->value());
}

std::string EscapeTitle(const std::string& input) {
  std::string escaped;
  escaped.reserve(input.size());
  for (char c : input) {
    if (c == '"' || c == '\\') {
      escaped.push_back('\\');
    }
    escaped.push_back(c);
  }
  return escaped;
}

std::string Trim(const std::string& input) {
  size_t start = input.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return std::string();
  }
  size_t end = input.find_last_not_of(" \t\r\n");
  return input.substr(start, end - start + 1);
}
}  // namespace

TabIpcServer* TabIpcServer::Get() {
  if (!g_ipc_server) {
    g_ipc_server = new TabIpcServer();
  }
  return g_ipc_server;
}

TabIpcServer::TabIpcServer() = default;

TabIpcServer::~TabIpcServer() {
  Stop();
}

void TabIpcServer::Start(const std::string& socket_path) {
  if (running_) {
    return;
  }
  socket_path_ = socket_path;
  listen_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listen_fd_ < 0) {
    LOG(ERROR) << "Failed to create IPC socket: " << std::strerror(errno);
    return;
  }

  ::unlink(socket_path_.c_str());

  sockaddr_un addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s",
                socket_path_.c_str());

  if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) <
      0) {
    LOG(ERROR) << "Failed to bind IPC socket: " << std::strerror(errno);
    close(listen_fd_);
    listen_fd_ = -1;
    return;
  }

  if (listen(listen_fd_, 5) < 0) {
    LOG(ERROR) << "Failed to listen on IPC socket: " << std::strerror(errno);
    close(listen_fd_);
    listen_fd_ = -1;
    return;
  }

  running_ = true;
  thread_ = std::thread(&TabIpcServer::ThreadMain, this);
}

void TabIpcServer::Stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  if (listen_fd_ >= 0) {
    close(listen_fd_);
    listen_fd_ = -1;
  }
  if (thread_.joinable()) {
    thread_.join();
  }
  if (!socket_path_.empty()) {
    ::unlink(socket_path_.c_str());
  }
}

void TabIpcServer::ThreadMain() {
  while (running_) {
    sockaddr_un client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd =
        accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr),
               &client_len);
    if (client_fd < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (!running_) {
        break;
      }
      continue;
    }
    HandleClient(client_fd);
    close(client_fd);
  }
}

void TabIpcServer::HandleClient(int client_fd) {
  std::string buffer;
  char chunk[512];
  ssize_t n = 0;
  while ((n = read(client_fd, chunk, sizeof(chunk))) > 0) {
    buffer.append(chunk, n);
    if (buffer.find('\n') != std::string::npos) {
      break;
    }
  }
  std::string response = HandleCommand(buffer);
  if (!response.empty()) {
    (void)write(client_fd, response.data(), response.size());
  }
}

std::string TabIpcServer::HandleCommand(const std::string& command) {
  std::string trimmed = Trim(command);
  if (trimmed.empty()) {
    return "ERR empty command\n";
  }

  std::istringstream stream(trimmed);
  std::string op;
  stream >> op;

  if (op == "get" || op == "list") {
    auto tabs = RunOnUiAndWait([&]() { return TabManager::Get()->GetTabs(); });
    std::ostringstream out;
    out << "OK\n";
    for (const auto& tab : tabs) {
      out << "id=" << tab.id << " active=" << (tab.active ? 1 : 0)
          << " url=" << tab.url << " title=\""
          << EscapeTitle(tab.title) << "\"\n";
    }
    return out.str();
  }

  if (op == "switch") {
    int target_id = 0;
    stream >> target_id;
    if (target_id <= 0) {
      return "ERR missing tab id\n";
    }
    bool success = RunOnUiAndWait(
        [target_id]() { return TabManager::Get()->ActivateTab(target_id); });
    if (!success) {
      return "ERR failed to switch tab\n";
    }
    return "OK\n";
  }

  if (op == "open") {
    std::string rest;
    std::getline(stream, rest);
    rest = Trim(rest);
    if (rest.empty()) {
      return "ERR missing URL\n";
    }
    int new_id = RunOnUiAndWait(
        [rest]() { return TabManager::Get()->OpenTab(rest, true); });
    if (new_id <= 0) {
      return "ERR failed to open tab\n";
    }
    std::ostringstream out;
    out << "OK id=" << new_id << "\n";
    return out.str();
  }

  return "ERR unknown command\n";
}

}  // namespace rethread
