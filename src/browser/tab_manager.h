#ifndef RETHREAD_BROWSER_TAB_MANAGER_H_
#define RETHREAD_BROWSER_TAB_MANAGER_H_

#include <memory>
#include <vector>

#include <QColor>
#include <QObject>
#include <QUrl>
#include <QString>
#include <QVector>
#include <QVariant>

class QStackedWidget;
class QWebEngineProfile;
class QWebEngineView;

namespace rethread {

class ContextMenuBindingManager;
class WebView;

class TabManager : public QObject {
  Q_OBJECT

 public:
  struct TabSnapshot {
    int id = 0;
    QString url;
    QString title;
    bool active = false;
  };

  TabManager(QWebEngineProfile* profile,
             const QColor& background_color,
             QObject* parent = nullptr);
  ~TabManager() override;

  void setContainer(QStackedWidget* stack);
  void setContextMenuBindingManager(ContextMenuBindingManager* manager);

  int openTab(const QUrl& url, bool activate, bool append_to_end = false);
  bool activateTab(int id);
  bool cycleActiveTab(int delta);
  QList<TabSnapshot> snapshot() const;
  bool closeTabAtIndex(int index);
  bool closeActiveTab();
  void closeAllTabs();
  bool historyBack();
  bool historyForward();
  bool EvaluateJavaScript(const QString& script,
                          int tab_id,
                          int tab_index,
                          QVariant* result,
                          QString* error_message);

  QWebEngineView* activeView() const;
  QWebEngineProfile* profile() const { return profile_; }

  QWebEngineView* createPopupTab();

 signals:
  void tabsChanged(const QList<TabSnapshot>& tabs);
  void allTabsClosed();

 private:
  struct TabEntry {
    int id = 0;
    QString url;
    QString title;
    bool active = false;
    WebView* view = nullptr;
  };

  TabEntry* findById(int id);
  const TabEntry* findById(int id) const;
  int activeIndex() const;
  void applyActiveState();
  void notifyTabsChanged();
  int nextTabId();
  bool closeById(int id);

  QWebEngineProfile* profile_ = nullptr;
  QColor background_color_;
  ContextMenuBindingManager* context_menu_binding_manager_ = nullptr;
  QStackedWidget* stack_ = nullptr;
  std::vector<std::unique_ptr<TabEntry>> tabs_;
  int next_tab_id_ = 1;
};

}  // namespace rethread

#endif  // RETHREAD_BROWSER_TAB_MANAGER_H_
