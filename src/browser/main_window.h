#ifndef RETHREAD_BROWSER_MAIN_WINDOW_H_
#define RETHREAD_BROWSER_MAIN_WINDOW_H_

#include <QMainWindow>
#include <QPointer>

class QStackedWidget;

namespace rethread {

class TabManager;
class TabStripOverlay;

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(TabManager& tab_manager, QWidget* parent = nullptr);

  QStackedWidget* tabStack() const;
 TabStripOverlay* tabStripOverlay() const;

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void closeEvent(QCloseEvent* event) override;

 private:
  void RepositionOverlay();

  TabManager& tab_manager_;
  QStackedWidget* stack_ = nullptr;
  QPointer<TabStripOverlay> overlay_;
};

}  // namespace rethread

#endif  // RETHREAD_BROWSER_MAIN_WINDOW_H_
