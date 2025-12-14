#include "browser/web_page.h"

#include <QWebEngineView>

#include "browser/tab_manager.h"

namespace rethread {

WebPage::WebPage(QWebEngineProfile* profile,
                 TabManager* manager,
                 QObject* parent)
    : QWebEnginePage(profile, parent), tab_manager_(manager) {}

QWebEnginePage* WebPage::createWindow(WebWindowType type) {
  Q_UNUSED(type);
  if (!tab_manager_) {
    return nullptr;
  }
  QWebEngineView* view = tab_manager_->createPopupTab();
  return view ? view->page() : nullptr;
}

}  // namespace rethread
