#include "app/app.h"

#include <fstream>
#include <string>

#include "browser/client.h"
#include "browser/tab_ipc_server.h"
#include "browser/tab_manager.h"
#include "browser/windowing.h"
#include "common/debug_log.h"
#include "common/theme.h"
#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/cef_command_line.h"
#include "include/cef_request_context.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

namespace rethread {
namespace {
constexpr char kUrlSwitch[] = "url";
constexpr char kDefaultUrl[] = "https://veilm.github.io/rethread/";

std::string ResolveStartupUrl(const CefRefPtr<CefCommandLine>& command_line) {
  std::string url = command_line->GetSwitchValue(kUrlSwitch);
  if (url.empty()) {
    return kDefaultUrl;
  }
  return url;
}

void PostAutoExitTask(int seconds) {
  if (seconds <= 0) {
    return;
  }
  CefPostDelayedTask(
      TID_UI,
      base::BindOnce(
          []() {
            if (auto* client = BrowserClient::Get()) {
              client->CloseAllBrowsers(true);
            } else {
              CefQuitMessageLoop();
            }
          }),
      seconds * 1000);
}

std::string Trim(const std::string& input) {
  size_t start = input.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return std::string();
  }
  size_t end = input.find_last_not_of(" \t\r\n");
  return input.substr(start, end - start + 1);
}

void RunStartupScript(const std::string& path) {
  if (path.empty()) {
    return;
  }
  std::ifstream input(path);
  if (!input.is_open()) {
    return;
  }
  AppendDebugLog("Running startup script: " + path);
  std::string line;
  while (std::getline(input, line)) {
    std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed[0] == '#') {
      continue;
    }
    std::string response = TabIpcServer::Get()->ExecuteCommand(trimmed);
    if (!response.empty()) {
      AppendDebugLog("startup cmd: " + trimmed + " -> " + Trim(response));
    }
  }
}
}  // namespace

RethreadApp::RethreadApp(const Options& options) : options_(options) {}

RethreadApp::~RethreadApp() {
  TabIpcServer::Get()->Stop();
}

void RethreadApp::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();

  auto command_line = CefCommandLine::GetGlobalCommandLine();

  BrowserClient::Options options;
  options.alloy_runtime = true;
  CefRefPtr<BrowserClient> client(new BrowserClient(options));

  const uint32_t background_color = GetDefaultBackgroundColor();

  auto* tab_manager = TabManager::Get();
  if (!tab_manager->IsInitialized()) {
    TabManager::InitParams params;
    params.client = client;
    params.request_context = CefRequestContext::GetGlobalContext();
    params.popup_delegate = new PopupWindowDelegate();
    params.background_color = background_color;
    tab_manager->Initialize(params);
  }

  tab_manager->OpenTab(ResolveStartupUrl(command_line), true);

  CefWindow::CreateTopLevelWindow(
      new MainWindowDelegate(CEF_SHOW_STATE_NORMAL));

  AppendDebugLog("Tab manager initialized.");
  if (!options_.tab_socket_path.empty()) {
    TabIpcServer::Get()->Start(options_.tab_socket_path);
  }
  RunStartupScript(options_.startup_script_path);
  if (options_.auto_exit_seconds > 0) {
    AppendDebugLog("Auto-exit scheduled for " +
                   std::to_string(options_.auto_exit_seconds) + " seconds.");
    PostAutoExitTask(options_.auto_exit_seconds);
  }
}

CefRefPtr<CefClient> RethreadApp::GetDefaultClient() {
  return BrowserClient::Get();
}

}  // namespace rethread
