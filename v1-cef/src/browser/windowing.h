#ifndef RETHREAD_BROWSER_WINDOWING_H_
#define RETHREAD_BROWSER_WINDOWING_H_

#include "browser/tab_strip.h"
#include "include/base/cef_macros.h"
#include "include/internal/cef_types.h"

#include "include/views/cef_browser_view.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_window.h"

namespace rethread {

// Hosts the primary browser view inside a top-level Views window.
class MainWindowDelegate : public CefWindowDelegate {
 public:
  explicit MainWindowDelegate(cef_show_state_t initial_state);
  MainWindowDelegate(CefRefPtr<CefBrowserView> browser_view,
                     cef_show_state_t initial_state);

  void OnWindowCreated(CefRefPtr<CefWindow> window) override;
  void OnWindowBoundsChanged(CefRefPtr<CefWindow> window,
                             const CefRect& new_bounds) override;
  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override;
  bool CanClose(CefRefPtr<CefWindow> window) override;
  CefSize GetPreferredSize(CefRefPtr<CefView> view) override;
  cef_show_state_t GetInitialShowState(CefRefPtr<CefWindow> window) override;
  cef_runtime_style_t GetWindowRuntimeStyle() override;

 private:
  const cef_show_state_t initial_state_;
  CefRefPtr<CefBrowserView> browser_view_;
  CefRefPtr<CefOverlayController> tab_overlay_;
  CefRefPtr<TabStripView> tab_strip_;

  void UpdateTabOverlayBounds(CefRefPtr<CefWindow> window);

  IMPLEMENT_REFCOUNTING(MainWindowDelegate);
  DISALLOW_COPY_AND_ASSIGN(MainWindowDelegate);
};

// Spawns out-of-band popup browsers in their own windows.
class PopupWindowDelegate : public CefBrowserViewDelegate {
 public:
  PopupWindowDelegate();

  bool OnPopupBrowserViewCreated(CefRefPtr<CefBrowserView> browser_view,
                                 CefRefPtr<CefBrowserView> popup_browser_view,
                                 bool is_devtools) override;
  cef_runtime_style_t GetBrowserRuntimeStyle() override;

 private:
  IMPLEMENT_REFCOUNTING(PopupWindowDelegate);
  DISALLOW_COPY_AND_ASSIGN(PopupWindowDelegate);
};

}  // namespace rethread

#endif  // RETHREAD_BROWSER_WINDOWING_H_
