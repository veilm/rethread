#ifndef RETHREAD_BROWSER_TAB_STRIP_OVERLAY_H_
#define RETHREAD_BROWSER_TAB_STRIP_OVERLAY_H_

#include <QFrame>
#include <QList>
#include <QString>
#include <QStringList>

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
  void SetCustomMessage(const QStringList& lines);
  void ClearCustomMessage();
  QSize sizeHint() const override;

 private:
  QString TruncateForDisplay(const QString& text) const;
  void Rebuild();

  QList<Entry> entries_;
  QStringList custom_lines_;
  bool showing_custom_message_ = false;
};

}  // namespace rethread

#endif  // RETHREAD_BROWSER_TAB_STRIP_OVERLAY_H_
