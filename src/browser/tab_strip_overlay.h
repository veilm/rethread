#ifndef RETHREAD_BROWSER_TAB_STRIP_OVERLAY_H_
#define RETHREAD_BROWSER_TAB_STRIP_OVERLAY_H_

#include <QFrame>
#include <QList>
#include <QString>

namespace rethread {

class TabStripOverlay : public QFrame {
  Q_OBJECT

 public:
  struct Entry {
    QString title;
    bool active = false;
  };

  explicit TabStripOverlay(QWidget* parent = nullptr);

  void SetTabs(const QList<Entry>& entries);
  QSize sizeHint() const override;

 private:
  void Rebuild();

  QList<Entry> entries_;
};

}  // namespace rethread

#endif  // RETHREAD_BROWSER_TAB_STRIP_OVERLAY_H_
