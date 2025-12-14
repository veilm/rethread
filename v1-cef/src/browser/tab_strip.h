#ifndef RETHREAD_BROWSER_TAB_STRIP_H_
#define RETHREAD_BROWSER_TAB_STRIP_H_

#include <string>
#include <vector>

#include "include/views/cef_box_layout.h"
#include "include/views/cef_button_delegate.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_panel_delegate.h"

namespace rethread {

class TabStripView : public CefPanelDelegate, public CefButtonDelegate {
 public:
  struct Tab {
    std::string title;
    bool active = false;
  };

  TabStripView();

  void SetTabs(const std::vector<Tab>& tabs);
  void Initialize();
  CefSize GetPreferredSize() const;

  CefRefPtr<CefPanel> GetPanel() const;

  // CefButtonDelegate
  void OnButtonPressed(CefRefPtr<CefButton> button) override {}

 private:
  void UpdateContent();

  std::vector<Tab> tabs_;
  CefRefPtr<CefPanel> panel_;

  IMPLEMENT_REFCOUNTING(TabStripView);
};

}  // namespace rethread

#endif  // RETHREAD_BROWSER_TAB_STRIP_H_
