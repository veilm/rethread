#include "browser/tab_ipc_server.h"

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <poll.h>
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
#include "common/debug_log.h"

namespace rethread {
namespace {
TabIpcServer* g_ipc_server = nullptr;

template <typename Func>
auto RunOnUiAndWait(Func&& func) -> decltype(func()) {
  using ReturnType = decltype(func());
  if (CefCurrentlyOn(TID_UI)) {
    return func();
  }

  TabIpcServer* server = g_ipc_server;
  if (server && server->IsStopping()) {
    return ReturnType();
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

  while (true) {
    if (event->TimedWait(50)) {
      break;
    }
    server = g_ipc_server;
    if (server && server->IsStopping()) {
      return ReturnType();
    }
  }

  if (!result_holder->has_value()) {
    return ReturnType();
  }

  return std::move(result_holder->value());
}

std::string JsonEscape(const std::string& input) {
  std::string escaped;
  escaped.reserve(input.size());
  for (unsigned char c : input) {
    switch (c) {
      case '\"':
        escaped.append("\\\"");
        break;
      case '\\':
        escaped.append("\\\\");
        break;
      case '\b':
        escaped.append("\\b");
        break;
      case '\f':
        escaped.append("\\f");
        break;
      case '\n':
        escaped.append("\\n");
        break;
      case '\r':
        escaped.append("\\r");
        break;
      case '\t':
        escaped.append("\\t");
        break;
      default:
        if (c < 0x20) {
          char buffer[7];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
          escaped.append(buffer);
        } else {
          escaped.push_back(static_cast<char>(c));
        }
        break;
    }
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
  Join();
}

void TabIpcServer::Start(const std::string& socket_path) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (running_.load(std::memory_order_acquire)) {
    return;
  }
  AppendDebugLog("TabIpcServer starting with socket " + socket_path);
  socket_path_ = socket_path;
  int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    LOG(ERROR) << "Failed to create IPC socket: " << std::strerror(errno);
    return;
  }

  ::unlink(socket_path_.c_str());

  sockaddr_un addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s",
                socket_path_.c_str());

  if (bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    LOG(ERROR) << "Failed to bind IPC socket: " << std::strerror(errno);
    close(listen_fd);
    return;
  }

  if (listen(listen_fd, 5) < 0) {
    LOG(ERROR) << "Failed to listen on IPC socket: " << std::strerror(errno);
    close(listen_fd);
    return;
  }

  if (pipe(wake_pipe_) != 0) {
    LOG(ERROR) << "Failed to create IPC wake pipe: " << std::strerror(errno);
    close(listen_fd);
    listen_fd = -1;
    return;
  }

  auto configure_pipe_end = [](int fd, bool make_non_blocking) {
    if (fd < 0) {
      return;
    }
    if (make_non_blocking) {
      int flags = fcntl(fd, F_GETFL, 0);
      if (flags >= 0 && !(flags & O_NONBLOCK)) {
        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
          LOG(WARNING) << "Failed to set O_NONBLOCK on wake pipe: "
                       << std::strerror(errno);
        }
      }
    }
    int fd_flags = fcntl(fd, F_GETFD, 0);
    if (fd_flags >= 0 && !(fd_flags & FD_CLOEXEC)) {
      if (fcntl(fd, F_SETFD, fd_flags | FD_CLOEXEC) != 0) {
        LOG(WARNING) << "Failed to set FD_CLOEXEC on wake pipe: "
                     << std::strerror(errno);
      }
    }
  };

  configure_pipe_end(wake_pipe_[0], true);
  configure_pipe_end(wake_pipe_[1], false);

  listen_fd_.store(listen_fd);
  running_.store(true, std::memory_order_release);
  stop_requested_.store(false, std::memory_order_release);
  thread_ = std::thread(&TabIpcServer::ThreadMain, this);
}

void TabIpcServer::Stop() {
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (!running_.load(std::memory_order_acquire) ||
      stop_requested_.load(std::memory_order_acquire)) {
    return;
  }
  AppendDebugLog("TabIpcServer stopping");
  stop_requested_.store(true, std::memory_order_release);
  running_.store(false, std::memory_order_release);
  int fd = listen_fd_.exchange(-1);
  if (fd >= 0) {
    shutdown(fd, SHUT_RDWR);
    close(fd);
  }
  NotifyWake();
}

void TabIpcServer::ThreadMain() {
  AppendDebugLog("TabIpcServer thread running");
  const int wake_fd = wake_pipe_[0];
  while (running_.load(std::memory_order_acquire)) {
    struct pollfd fds[2];
    int nfds = 0;
    const int listen_fd = listen_fd_.load();
    if (listen_fd >= 0) {
      fds[nfds++] = {listen_fd, POLLIN, 0};
    }
    if (wake_fd >= 0) {
      fds[nfds++] = {wake_fd, POLLIN, 0};
    }
    if (nfds == 0) {
      break;
    }
    int rv = poll(fds, nfds, -1);
    if (rv < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (!running_.load(std::memory_order_acquire)) {
        break;
      }
      continue;
    }

    bool handled_event = false;
    for (int i = 0; i < nfds; ++i) {
      if (fds[i].fd == wake_fd && (fds[i].revents & POLLIN)) {
        handled_event = true;
        char buf[32];
        while (true) {
          ssize_t drained = read(wake_fd, buf, sizeof(buf));
          if (drained > 0) {
            continue;
          }
          if (drained < 0 && errno == EINTR) {
            continue;
          }
          if (drained <= 0 &&
              (errno == EAGAIN || errno == EWOULDBLOCK || drained == 0)) {
            break;
          }
          break;
        }
        continue;
      }
      if (fds[i].fd == listen_fd &&
          (fds[i].revents & (POLLIN | POLLERR | POLLHUP))) {
        sockaddr_un client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd =
            accept(listen_fd, reinterpret_cast<sockaddr*>(&client_addr),
                   &client_len);
        if (client_fd < 0) {
          if (errno == EINTR) {
            continue;
          }
          if (!running_.load(std::memory_order_acquire)) {
            break;
          }
          continue;
        }
        handled_event = true;
        AppendDebugLog("TabIpcServer accepted client");
        HandleClient(client_fd);
        close(client_fd);
      }
    }

    if (!handled_event && !running_.load(std::memory_order_acquire)) {
      break;
    }
  }
  AppendDebugLog("TabIpcServer thread exiting");
}

void TabIpcServer::Join() {
  std::thread worker;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    worker = std::move(thread_);
  }
  if (worker.joinable()) {
    worker.join();
  }
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    CloseWakePipe();
    if (!socket_path_.empty()) {
      ::unlink(socket_path_.c_str());
    }
    stop_requested_.store(false, std::memory_order_release);
  }
}

bool TabIpcServer::IsStopping() const {
  return stop_requested_.load(std::memory_order_acquire);
}

void TabIpcServer::NotifyWake() {
  if (wake_pipe_[1] < 0) {
    return;
  }
  const char byte = 1;
  (void)write(wake_pipe_[1], &byte, 1);
}

void TabIpcServer::CloseWakePipe() {
  if (wake_pipe_[0] >= 0) {
    close(wake_pipe_[0]);
    wake_pipe_[0] = -1;
  }
  if (wake_pipe_[1] >= 0) {
    close(wake_pipe_[1]);
    wake_pipe_[1] = -1;
  }
}

void TabIpcServer::HandleClient(int client_fd) {
  int flags = fcntl(client_fd, F_GETFL, 0);
  if (flags >= 0 && !(flags & O_NONBLOCK)) {
    if (fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) != 0) {
      LOG(WARNING) << "Failed to set client socket non-blocking: "
                   << std::strerror(errno);
    }
  }

  std::string buffer;
  char chunk[512];
  while (true) {
    if (stop_requested_.load(std::memory_order_acquire)) {
      AppendDebugLog("TabIpcServer aborting client read due to shutdown");
      break;
    }

    ssize_t n = read(client_fd, chunk, sizeof(chunk));
    if (n > 0) {
      buffer.append(chunk, n);
      if (buffer.find('\n') != std::string::npos) {
        break;
      }
      continue;
    }
    if (n == 0) {
      break;
    }
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        struct pollfd pfd;
        pfd.fd = client_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int rv = poll(&pfd, 1, 100);
        if (rv <= 0) {
          if (rv < 0 && errno == EINTR) {
            continue;
          }
          if (rv == 0) {
            continue;
          }
          break;
        }
        short error_bits = pfd.revents & (POLLERR | POLLHUP);
#ifdef POLLRDHUP
        error_bits |= (pfd.revents & POLLRDHUP);
#endif
        if (error_bits) {
          break;
        }
        continue;
      }
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
    out << "{\n  \"tabs\": [";
    for (size_t i = 0; i < tabs.size(); ++i) {
      const auto& tab = tabs[i];
      if (i == 0) {
        out << "\n";
      }
      out << "    {\"id\": " << tab.id << ", \"active\": "
          << (tab.active ? "true" : "false") << ", \"url\": \""
          << JsonEscape(tab.url) << "\", \"title\": \""
          << JsonEscape(tab.title) << "\"}";
      if (i + 1 < tabs.size()) {
        out << ",";
      }
      out << "\n";
    }
    out << "  ]\n}\n";
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

  if (op == "cycle") {
    int delta = 0;
    if (!(stream >> delta)) {
      return "ERR missing tab delta\n";
    }
    bool success = RunOnUiAndWait([delta]() {
      auto* manager = TabManager::Get();
      return manager->CycleActiveTab(delta);
    });
    if (!success) {
      return "ERR failed to cycle tab\n";
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

  if (op == "tabstrip") {
    std::string action;
    stream >> action;
    if (action.empty()) {
      return "ERR missing tabstrip action\n";
    }

    auto run_action = [&](auto&& fn) -> std::string {
      bool success = RunOnUiAndWait([&]() {
        fn();
        return true;
      });
      if (!success) {
        return "ERR failed to execute tabstrip action\n";
      }
      return "OK\n";
    };

    if (action == "show") {
      return run_action([]() { TabManager::Get()->ShowTabStrip(); });
    }
    if (action == "hide") {
      return run_action([]() { TabManager::Get()->HideTabStrip(); });
    }
    if (action == "toggle") {
      return run_action([]() { TabManager::Get()->ToggleTabStrip(); });
    }
    if (action == "peek") {
      int duration_ms = 0;
      stream >> duration_ms;
      if (duration_ms <= 0) {
        return "ERR tabstrip peek requires duration in ms\n";
      }
      return run_action([duration_ms]() {
        TabManager::Get()->ShowTabStripForDuration(duration_ms);
      });
    }
    return "ERR unknown tabstrip action\n";
  }

  return "ERR unknown command\n";
}

}  // namespace rethread
