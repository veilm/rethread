#ifndef RETHREAD_BROWSER_TAB_STRIP_CONTROLLER_H_
#define RETHREAD_BROWSER_TAB_STRIP_CONTROLLER_H_

#include <QObject>
#include <QPointer>
#include <QTimer>

#include "browser/tab_manager.h"
#include "browser/tab_strip_overlay.h"

namespace rethread {

class TabStripController : public QObject {
  Q_OBJECT

 public:
  explicit TabStripController(TabStripOverlay* overlay,
                              QObject* parent = nullptr);

  void SetTabs(const QList<TabManager::TabSnapshot>& tabs);
  void Show();
  void Hide();
  void Toggle();
  void Peek(int milliseconds);

 private:
  void ApplyVisibility(bool visible);
  void CancelPendingHide();

  QPointer<TabStripOverlay> overlay_;
  QList<TabStripOverlay::Entry> current_entries_;
  int visibility_token_ = 0;
  QTimer hide_timer_;
  bool visible_ = false;
};

}  // namespace rethread

#endif  // RETHREAD_BROWSER_TAB_STRIP_CONTROLLER_H_
