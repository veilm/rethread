#include "browser/windowing.h"

#include "include/cef_browser.h"

#include "common/theme.h"

namespace rethread {
namespace {
constexpr cef_runtime_style_t kRuntimeStyle = CEF_RUNTIME_STYLE_ALLOY;
constexpr int kDefaultWidth = 800;
constexpr int kDefaultHeight = 600;
constexpr int kTabStripWidth = 240;
constexpr cef_color_t kTabBackgroundColor = CefColorSetARGB(224, 32, 32, 32);
constexpr cef_color_t kTabForegroundColor = CefColorSetARGB(255, 240, 240, 240);
}  // namespace

MainWindowDelegate::MainWindowDelegate(CefRefPtr<CefBrowserView> browser_view,
                                       cef_show_state_t initial_state)
    : browser_view_(browser_view), initial_state_(initial_state) {}

void MainWindowDelegate::OnWindowCreated(CefRefPtr<CefWindow> window) {
  window->SetBackgroundColor(GetDefaultBackgroundColor());
  window->AddChildView(browser_view_);
  if (initial_state_ != CEF_SHOW_STATE_HIDDEN) {
    window->Show();
  }

  tab_strip_ = new TabStripView();
  tab_strip_->Initialize();
  tab_strip_->GetPanel()->SetBackgroundColor(kTabBackgroundColor);
  tab_strip_->SetTabs({{"github.com/veilm/rethread", true},
                       {"news.ycombinator.com", false}});
  tab_overlay_ = window->AddOverlayView(tab_strip_->GetPanel(),
                                        CEF_DOCKING_MODE_CUSTOM,
                                        false);
  UpdateTabOverlayBounds(window);
  tab_overlay_->SetVisible(true);
}

void MainWindowDelegate::OnWindowBoundsChanged(CefRefPtr<CefWindow> window,
                                               const CefRect& new_bounds) {
  UpdateTabOverlayBounds(window);
}

void MainWindowDelegate::OnWindowDestroyed(CefRefPtr<CefWindow> window) {
  tab_overlay_ = nullptr;
  tab_strip_ = nullptr;
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

void MainWindowDelegate::UpdateTabOverlayBounds(
    CefRefPtr<CefWindow> window) {
  if (!tab_overlay_ || !window) {
    return;
  }
  CefRect bounds = window->GetBounds();
  CefRect tab_bounds;
  tab_bounds.Set(0, 0, kTabStripWidth, bounds.height);
  tab_overlay_->SetBounds(tab_bounds);
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
