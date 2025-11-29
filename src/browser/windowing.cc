#include "browser/windowing.h"

#include "include/cef_browser.h"

namespace rethread {
namespace {
constexpr cef_runtime_style_t kRuntimeStyle = CEF_RUNTIME_STYLE_ALLOY;
constexpr int kDefaultWidth = 800;
constexpr int kDefaultHeight = 600;
}

MainWindowDelegate::MainWindowDelegate(CefRefPtr<CefBrowserView> browser_view,
                                       cef_show_state_t initial_state)
    : browser_view_(browser_view), initial_state_(initial_state) {}

void MainWindowDelegate::OnWindowCreated(CefRefPtr<CefWindow> window) {
  window->AddChildView(browser_view_);
  if (initial_state_ != CEF_SHOW_STATE_HIDDEN) {
    window->Show();
  }
}

void MainWindowDelegate::OnWindowDestroyed(CefRefPtr<CefWindow> window) {
  browser_view_ = nullptr;
}

bool MainWindowDelegate::CanClose(CefRefPtr<CefWindow> window) {
  CefRefPtr<CefBrowser> browser = browser_view_->GetBrowser();
  if (!browser) {
    return true;
  }
  return browser->GetHost()->TryCloseBrowser();
}

CefSize MainWindowDelegate::GetPreferredSize(CefRefPtr<CefView> view) {
  return CefSize(kDefaultWidth, kDefaultHeight);
}

cef_show_state_t MainWindowDelegate::GetInitialShowState(
    CefRefPtr<CefWindow> window) {
  return initial_state_;
}

cef_runtime_style_t MainWindowDelegate::GetWindowRuntimeStyle() {
  return kRuntimeStyle;
}

PopupWindowDelegate::PopupWindowDelegate() = default;

bool PopupWindowDelegate::OnPopupBrowserViewCreated(
    CefRefPtr<CefBrowserView> browser_view,
    CefRefPtr<CefBrowserView> popup_browser_view,
    bool is_devtools) {
  CefWindow::CreateTopLevelWindow(new MainWindowDelegate(
      popup_browser_view, CEF_SHOW_STATE_NORMAL));
  return true;
}

cef_runtime_style_t PopupWindowDelegate::GetBrowserRuntimeStyle() {
  return kRuntimeStyle;
}

}  // namespace rethread
