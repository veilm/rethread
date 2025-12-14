#include "browser/main_window.h"

#include <algorithm>

#include <QCloseEvent>
#include <QEvent>
#include <QMetaObject>
#include <QResizeEvent>
#include <QStackedWidget>

#include "browser/tab_manager.h"
#include "browser/tab_strip_overlay.h"

namespace rethread {
namespace {
constexpr int kDefaultWidth = 1024;
constexpr int kDefaultHeight = 720;
}

MainWindow::MainWindow(TabManager& tab_manager, QWidget* parent)
    : QMainWindow(parent), tab_manager_(tab_manager) {
  stack_ = new QStackedWidget(this);
  setCentralWidget(stack_);
  resize(kDefaultWidth, kDefaultHeight);

  overlay_ = new TabStripOverlay(this);
  overlay_->hide();
  overlay_->installEventFilter(this);
}

QStackedWidget* MainWindow::tabStack() const {
  return stack_;
}

TabStripOverlay* MainWindow::tabStripOverlay() const {
  return overlay_;
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if (watched == overlay_ &&
      (event->type() == QEvent::LayoutRequest ||
       event->type() == QEvent::Resize)) {
    QMetaObject::invokeMethod(this, [this]() { RepositionOverlay(); },
                              Qt::QueuedConnection);
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
  RepositionOverlay();
}

void MainWindow::closeEvent(QCloseEvent* event) {
  tab_manager_.closeAllTabs();
  QMainWindow::closeEvent(event);
}

void MainWindow::RepositionOverlay() {
  if (!overlay_) {
    return;
  }
  QSize size = overlay_->sizeHint();
  const QSize window_size = this->size();
  const int width = std::min(size.width(), window_size.width());
  const int height = std::min(size.height(), window_size.height());
  const int x = (window_size.width() - width) / 2;
  const int y = (window_size.height() - height) / 2;
  overlay_->setGeometry(x, y, width, height);
}

}  // namespace rethread
