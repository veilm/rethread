#include "app/app.h"

#include <string>

#include "browser/client.h"
#include "browser/windowing.h"
#include "include/cef_command_line.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"

namespace rethread {
namespace {
constexpr char kUrlSwitch[] = "url";
constexpr char kInitialShowStateSwitch[] = "initial-show-state";
constexpr char kDefaultUrl[] = "https://github.com/veilm/rethread";

std::string ResolveStartupUrl(const CefRefPtr<CefCommandLine>& command_line) {
  std::string url = command_line->GetSwitchValue(kUrlSwitch);
  if (url.empty()) {
    return kDefaultUrl;
  }
  return url;
}

cef_show_state_t ResolveShowState(const CefRefPtr<CefCommandLine>& command_line) {
  const std::string value = command_line->GetSwitchValue(kInitialShowStateSwitch);
  if (value == "minimized") {
    return CEF_SHOW_STATE_MINIMIZED;
  }
  if (value == "maximized") {
    return CEF_SHOW_STATE_MAXIMIZED;
  }
#if defined(OS_MAC)
  if (value == "hidden") {
    return CEF_SHOW_STATE_HIDDEN;
  }
#endif
  return CEF_SHOW_STATE_NORMAL;
}

}  // namespace

RethreadApp::RethreadApp() = default;

void RethreadApp::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();

  auto command_line = CefCommandLine::GetGlobalCommandLine();

  BrowserClient::Options options;
  options.alloy_runtime = true;
  CefRefPtr<BrowserClient> client(new BrowserClient(options));

  CefBrowserSettings browser_settings;

  CefRefPtr<CefBrowserView> browser_view = CefBrowserView::CreateBrowserView(
      client, ResolveStartupUrl(command_line), browser_settings, nullptr, nullptr,
      new PopupWindowDelegate());

  CefWindow::CreateTopLevelWindow(new MainWindowDelegate(
      browser_view, ResolveShowState(command_line)));
}

CefRefPtr<CefClient> RethreadApp::GetDefaultClient() {
  return BrowserClient::Get();
}

}  // namespace rethread
