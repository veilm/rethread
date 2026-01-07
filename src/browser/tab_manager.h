#ifndef RETHREAD_BROWSER_TAB_MANAGER_H_
#define RETHREAD_BROWSER_TAB_MANAGER_H_

#include <memory>
#include <vector>

#include <QColor>
#include <QPointer>
#include <QObject>
#include <QUrl>
#include <QString>
#include <QVector>
#include <QVariant>
#include <unordered_map>

#include <QWebChannel>

class QStackedWidget;
class QWebEngineProfile;
class QWebEngineView;
class QWebEnginePage;

namespace rethread {

class ContextMenuBindingManager;
class RulesManager;
class WebView;
class JsEvalBridge;

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
  void setRulesManager(RulesManager* manager);

  int openTab(const QUrl& url, bool activate, bool append_to_end = false);
  bool activateTab(int id);
  bool cycleActiveTab(int delta);
  QList<TabSnapshot> snapshot() const;
  bool SwapTabs(int first_index, int second_index);
  bool closeTabAtIndex(int index);
  bool closeActiveTab();
  void closeAllTabs();
  bool historyBack();
  bool historyForward();
  bool OpenDevToolsForActiveTab();
  QString DevToolsIdForTab(int tab_id) const;
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
    std::unique_ptr<QWebChannel> eval_channel;
    std::unique_ptr<JsEvalBridge> eval_bridge;
    int next_eval_request_id = 1;
    bool eval_bridge_ready = false;
  };

  TabEntry* findById(int id);
  const TabEntry* findById(int id) const;
  int activeIndex() const;
  void applyActiveState();
  void notifyTabsChanged();
  int nextTabId();
  bool closeById(int id);
  void ApplyRulesToView(WebView* view, const QUrl& url) const;
  void ApplyRulesToAllTabs() const;
  void CloseDevTools(QWebEnginePage* page, bool close_view);
  void EnsureEvalBridge(TabEntry* tab);

  struct DevToolsWindow {
    QPointer<QWebEngineView> view;
    QPointer<QWebEnginePage> inspected_page;
    QPointer<QWebEnginePage> devtools_page;
  };

  QWebEngineProfile* profile_ = nullptr;
  QColor background_color_;
  ContextMenuBindingManager* context_menu_binding_manager_ = nullptr;
  RulesManager* rules_manager_ = nullptr;
  QStackedWidget* stack_ = nullptr;
  std::vector<std::unique_ptr<TabEntry>> tabs_;
  int next_tab_id_ = 1;
  std::unordered_map<QWebEnginePage*, DevToolsWindow> devtools_windows_;
};

}  // namespace rethread

#endif  // RETHREAD_BROWSER_TAB_MANAGER_H_
