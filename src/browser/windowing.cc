#include "browser/windowing.h"

#include <algorithm>

#include "include/cef_browser.h"

#include "browser/tab_manager.h"
#include "common/theme.h"

namespace rethread {
namespace {
constexpr cef_runtime_style_t kRuntimeStyle = CEF_RUNTIME_STYLE_ALLOY;
constexpr int kDefaultWidth = 800;
constexpr int kDefaultHeight = 600;
constexpr int kDefaultTabStripWidth = 260;
constexpr int kDefaultTabStripHeight = 200;
constexpr cef_color_t kTabBackgroundColor = CefColorSetARGB(224, 32, 32, 32);
constexpr cef_color_t kTabForegroundColor = CefColorSetARGB(255, 240, 240, 240);
}  // namespace

MainWindowDelegate::MainWindowDelegate(cef_show_state_t initial_state)
    : initial_state_(initial_state) {}

MainWindowDelegate::MainWindowDelegate(CefRefPtr<CefBrowserView> browser_view,
                                       cef_show_state_t initial_state)
    : browser_view_(browser_view), initial_state_(initial_state) {}

void MainWindowDelegate::OnWindowCreated(CefRefPtr<CefWindow> window) {
  window->SetBackgroundColor(GetDefaultBackgroundColor());
  if (browser_view_) {
    window->AddChildView(browser_view_);
  } else if (auto* tab_manager = TabManager::Get()) {
    tab_manager->AttachWindow(window);
  }
  if (initial_state_ != CEF_SHOW_STATE_HIDDEN) {
    window->Show();
  }

  if (!browser_view_) {
    tab_strip_ = new TabStripView();
    tab_strip_->Initialize();
    CefRefPtr<CefPanel> tab_panel = tab_strip_->GetPanel();
    if (tab_panel) {
      tab_panel->SetBackgroundColor(kTabBackgroundColor);
    }
    if (auto* tab_manager = TabManager::Get()) {
      tab_manager->BindTabStrip(tab_strip_);
    }
    if (tab_panel) {
      tab_overlay_ =
          window->AddOverlayView(tab_panel, CEF_DOCKING_MODE_CUSTOM, false);
    }
    UpdateTabOverlayBounds(window);
    if (tab_overlay_) {
      tab_overlay_->SetVisible(true);
    }
  }
}

void MainWindowDelegate::OnWindowBoundsChanged(CefRefPtr<CefWindow> window,
                                               const CefRect& new_bounds) {
  UpdateTabOverlayBounds(window);
}

void MainWindowDelegate::OnWindowDestroyed(CefRefPtr<CefWindow> window) {
  if (!browser_view_) {
    if (auto* tab_manager = TabManager::Get()) {
      tab_manager->UnbindTabStrip(tab_strip_);
      tab_manager->DetachWindow(window);
    }
  }
  tab_overlay_ = nullptr;
  tab_strip_ = nullptr;
  browser_view_ = nullptr;
}

bool MainWindowDelegate::CanClose(CefRefPtr<CefWindow> window) {
  if (browser_view_) {
    CefRefPtr<CefBrowser> browser = browser_view_->GetBrowser();
    if (!browser) {
      return true;
    }
    return browser->GetHost()->TryCloseBrowser();
  }
  if (auto* tab_manager = TabManager::Get()) {
    CefRefPtr<CefBrowser> browser = tab_manager->GetActiveBrowser();
    if (browser) {
      return browser->GetHost()->TryCloseBrowser();
    }
  }
  return true;
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
  if (!tab_overlay_ || !window || !tab_strip_) {
    return;
  }
  CefRect bounds = window->GetBounds();
  CefSize preferred = tab_strip_->GetPreferredSize();
  int width = preferred.width > 0 ? preferred.width : kDefaultTabStripWidth;
  int height =
      preferred.height > 0 ? preferred.height : kDefaultTabStripHeight;
  width = std::min(width, bounds.width);
  height = std::min(height, bounds.height);

  int x = (bounds.width - width) / 2;
  int y = (bounds.height - height) / 2;

  tab_overlay_->SetBounds(CefRect(x, y, width, height));
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
