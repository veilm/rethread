#ifndef RETHREAD_BROWSER_TAB_STRIP_H_
#define RETHREAD_BROWSER_TAB_STRIP_H_

#include <string>
#include <vector>

#include "include/views/cef_box_layout.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_panel_delegate.h"
#include "include/views/cef_textfield.h"

namespace rethread {

class TabStripView : public CefPanelDelegate {
 public:
  struct Tab {
    std::string title;
    bool active = false;
  };

  TabStripView();

  void SetTabs(const std::vector<Tab>& tabs);
  void Initialize();

  CefRefPtr<CefPanel> GetPanel() const { return panel_; }

 private:
  void UpdateContent();

  std::vector<Tab> tabs_;
  CefRefPtr<CefPanel> panel_;

  IMPLEMENT_REFCOUNTING(TabStripView);
};

}  // namespace rethread

#endif  // RETHREAD_BROWSER_TAB_STRIP_H_
