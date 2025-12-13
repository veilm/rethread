#ifndef RETHREAD_BROWSER_TAB_MANAGER_H_
#define RETHREAD_BROWSER_TAB_MANAGER_H_

#include <memory>
#include <string>
#include <vector>

#include "include/base/cef_macros.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_request_context.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"

namespace rethread {

class TabStripView;

class TabManager {
 public:
  struct InitParams {
    CefRefPtr<CefClient> client;
    CefRefPtr<CefRequestContext> request_context;
    CefRefPtr<CefBrowserViewDelegate> popup_delegate;
    uint32_t background_color = 0;
  };

  struct TabSnapshot {
    int id = 0;
    std::string url;
    std::string title;
    bool active = false;
  };

  static TabManager* Get();

  void Initialize(const InitParams& params);
  bool IsInitialized() const { return initialized_; }

  int OpenTab(const std::string& url, bool activate);
  bool ActivateTab(int id);
  std::vector<TabSnapshot> GetTabs() const;
  CefRefPtr<CefBrowser> GetActiveBrowser() const;
  void CloseAllTabs(bool force_close);
  CefRefPtr<CefWindow> GetWindow() const { return window_; }

  void AttachWindow(CefRefPtr<CefWindow> window);
  void DetachWindow(CefRefPtr<CefWindow> window);
  void BindTabStrip(CefRefPtr<TabStripView> tab_strip,
                    CefRefPtr<CefOverlayController> overlay);
  void UnbindTabStrip(CefRefPtr<TabStripView> tab_strip);

  void UpdateBrowserTitle(CefRefPtr<CefBrowser> browser,
                          const std::string& title);

  void ShowTabStrip();
  void HideTabStrip();
  void ToggleTabStrip();
  void ShowTabStripForDuration(int milliseconds);

 private:
  struct TabEntry {
    int id = 0;
    std::string url;
    std::string title;
    bool active = false;
    CefRefPtr<CefBrowserView> view;
  };

  TabManager();

  void EnsureContentPanel();
  void AttachTabView(TabEntry* tab);
  void ApplyActiveState();
  void UpdateTabStrip() const;
  void ApplyTabStripVisibility();
  void SetTabStripVisible(bool visible);
  int NextTabStripVisibilityToken();
  void HandleTabStripFlashTimeout(int token);

  TabEntry* FindTabById(int id);
  const TabEntry* FindTabById(int id) const;
  TabEntry* FindTabByBrowser(CefRefPtr<CefBrowser> browser);

  CefRefPtr<CefClient> client_;
  CefRefPtr<CefRequestContext> request_context_;
  CefRefPtr<CefBrowserViewDelegate> popup_delegate_;
  uint32_t background_color_ = 0;
  bool initialized_ = false;

  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefPanel> content_panel_;
  CefRefPtr<TabStripView> tab_strip_;
  CefRefPtr<CefOverlayController> tab_overlay_;
  bool tab_strip_visible_ = false;
  int tab_strip_visibility_token_ = 0;

  std::vector<std::unique_ptr<TabEntry>> tabs_;
  int next_tab_id_ = 1;

  TabManager(const TabManager&) = delete;
  TabManager& operator=(const TabManager&) = delete;
};

}  // namespace rethread

#endif  // RETHREAD_BROWSER_TAB_MANAGER_H_
