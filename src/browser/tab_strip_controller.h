#ifndef RETHREAD_BROWSER_TAB_STRIP_CONTROLLER_H_
#define RETHREAD_BROWSER_TAB_STRIP_CONTROLLER_H_

#include <QObject>
#include <QPointer>
#include <QStringList>
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
  void ShowMessage(const QStringList& lines, int duration_ms);

 private:
  void ApplyVisibility(bool visible);
  void CancelPendingHide();
  void ClearCustomMessage();

  QPointer<TabStripOverlay> overlay_;
  QList<TabStripOverlay::Entry> current_entries_;
  QStringList custom_message_lines_;
  int visibility_token_ = 0;
  QTimer hide_timer_;
  bool visible_ = false;
  bool showing_custom_message_ = false;
};

}  // namespace rethread

#endif  // RETHREAD_BROWSER_TAB_STRIP_CONTROLLER_H_
