#ifndef RETHREAD_BROWSER_WEB_PAGE_H_
#define RETHREAD_BROWSER_WEB_PAGE_H_

#include <QWebEnginePage>

namespace rethread {

class TabManager;

class WebPage : public QWebEnginePage {
  Q_OBJECT

 public:
  WebPage(QWebEngineProfile* profile, TabManager* manager, QObject* parent = nullptr);

 protected:
  QWebEnginePage* createWindow(WebWindowType type) override;

 private:
  TabManager* tab_manager_;
};

}  // namespace rethread

#endif  // RETHREAD_BROWSER_WEB_PAGE_H_
