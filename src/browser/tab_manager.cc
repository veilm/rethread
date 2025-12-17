#include "browser/tab_manager.h"

#include <algorithm>

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>
#include <QWebChannel>
#include <QWebEngineHistory>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <QDebug>

#include "browser/context_menu_binding_manager.h"
#include "browser/js_eval_bridge.h"
#include "browser/rules_manager.h"
#include "browser/web_page.h"
#include "browser/web_view.h"

namespace rethread {
namespace {
QString TabTitleOrUrl(const QString& title, const QString& url) {
  return title.isEmpty() ? url : title;
}

QString EvalHelperSource() {
  static QString cached;
  if (!cached.isEmpty()) {
    return cached;
  }

  QString script;
  QFile qweb_file(QStringLiteral(":/qtwebchannel/qwebchannel.js"));
  if (qweb_file.open(QIODevice::ReadOnly)) {
    script.append(QString::fromUtf8(qweb_file.readAll()));
    qweb_file.close();
  } else {
    qWarning() << "Failed to load qwebchannel.js for eval bridge";
  }

  script.append(QStringLiteral(R"JS(
(function() {
  if (window.__rethreadEvalBridgeInstalled) {
    return;
  }
  window.__rethreadEvalBridgeInstalled = true;
  function install(channel) {
    if (!channel.__rethreadPatched) {
      channel.__rethreadPatched = true;
      var storedCallbacks = channel.execCallbacks || {};
      var noop = function() {};
      channel.execCallbacks = new Proxy(storedCallbacks, {
        get: function(target, prop) {
          var value = target[prop];
          return typeof value === 'function' ? value : noop;
        },
        set: function(target, prop, value) {
          target[prop] = value;
          return true;
        },
        deleteProperty: function(target, prop) {
          delete target[prop];
          return true;
        }
      });
    }

    var bridge = channel.objects.rethreadEvalBridge;
    if (!bridge) {
      console.warn('[rethread] eval helper missing bridge object; objects=', Object.keys(channel.objects || {}));
      return;
    }

    var evalSignal = bridge.evalRequested || bridge.EvalRequested;
    var resolveMethod = bridge.resolve || bridge.Resolve;
    var rejectMethod = bridge.reject || bridge.Reject;
    var readyMethod = bridge.notifyReady || bridge.NotifyReady;
    if (!evalSignal || !resolveMethod || !rejectMethod || !readyMethod) {
      console.warn('[rethread] eval helper missing slots/signals');
      return;
    }

    readyMethod.call(bridge);

    evalSignal.connect(function(requestId, source) {
      var finished = false;
      function finish(ok, value) {
        if (finished || !bridge) {
          return;
        }
        finished = true;
        if (ok) {
          resolveMethod.call(bridge, requestId, value);
        } else {
          var message = value;
          if (message && typeof message === 'object' && message.message) {
            message = message.message;
          }
          rejectMethod.call(bridge, requestId, String(message !== undefined ? message : 'Unknown error'));
        }
      }
      try {
        var result = (0, eval)(source);
        if (result && typeof result.then === 'function') {
          Promise.resolve(result).then(function(value) {
            finish(true, value);
          }, function(err) {
            finish(false, err);
          });
        } else {
          finish(true, result);
        }
      } catch (error) {
        finish(false, error);
      }
    });
  }

  function initChannel() {
    if (!window.qt || !window.qt.webChannelTransport) {
      window.setTimeout(initChannel, 50);
      return;
    }
    new QWebChannel(window.qt.webChannelTransport, install);
  }

  initChannel();
})();
)JS"));

  cached = script;
  return cached;
}

void InstallEvalHelperScript(QWebEnginePage* page) {
  if (!page) {
    return;
  }
  QWebEngineScriptCollection* collection = &page->scripts();
  const auto existing =
      collection->find(QStringLiteral("rethread_eval_helper"));
  if (!existing.isEmpty()) {
    return;
  }

  QWebEngineScript script;
  script.setName(QStringLiteral("rethread_eval_helper"));
  script.setInjectionPoint(QWebEngineScript::DocumentCreation);
  script.setRunsOnSubFrames(false);
  script.setWorldId(QWebEngineScript::MainWorld);
  script.setSourceCode(EvalHelperSource());
  collection->insert(script);
  page->runJavaScript(EvalHelperSource(), QWebEngineScript::MainWorld);
}

constexpr int kEvalReadyTimeoutMs = 3000;
}  // namespace

TabManager::TabManager(QWebEngineProfile* profile,
                       const QColor& background_color,
                       QObject* parent)
    : QObject(parent),
      profile_(profile),
      background_color_(background_color) {}

TabManager::~TabManager() {
  closeAllTabs();
}

void TabManager::setContainer(QStackedWidget* stack) {
  stack_ = stack;
  if (!stack_) {
    return;
  }
  for (const auto& tab : tabs_) {
    if (tab->view) {
      tab->view->setParent(stack_);
      if (stack_->indexOf(tab->view) == -1) {
        stack_->addWidget(tab->view);
      }
      tab->view->setSizePolicy(QSizePolicy::Expanding,
                               QSizePolicy::Expanding);
      tab->view->resize(stack_->size());
      tab->view->setVisible(tab->active);
    }
  }
  applyActiveState();
}

void TabManager::setContextMenuBindingManager(
    ContextMenuBindingManager* manager) {
  context_menu_binding_manager_ = manager;
}

void TabManager::setRulesManager(RulesManager* manager) {
  if (rules_manager_ == manager) {
    return;
  }
  if (rules_manager_) {
    disconnect(rules_manager_, nullptr, this, nullptr);
  }
  rules_manager_ = manager;
  if (rules_manager_) {
    connect(rules_manager_, &RulesManager::javaScriptRulesChanged, this,
            &TabManager::ApplyRulesToAllTabs);
  }
  ApplyRulesToAllTabs();
}

int TabManager::openTab(const QUrl& url, bool activate, bool append_to_end) {
  if (!profile_) {
    return -1;
  }

  const int prior_active_index = activeIndex();
  auto tab = std::make_unique<TabEntry>();
  tab->id = nextTabId();
  tab->active = tabs_.empty() || activate;

  auto* view =
      new WebView(context_menu_binding_manager_, background_color_);
  auto* page = new WebPage(profile_, this, view);
  page->setBackgroundColor(background_color_);
  view->setPage(page);
  view->BindPageSignals(page);
  tab->view = view;
  tab->url = url.isEmpty() ? QStringLiteral("about:blank") : url.toString();
  tab->title = tab->url;

  TabEntry* tab_ptr = tab.get();
  QObject::connect(view, &QWebEngineView::titleChanged, this,
                   [this, tab_ptr](const QString& title) {
                     tab_ptr->title = TabTitleOrUrl(title, tab_ptr->url);
                     notifyTabsChanged();
                   });
  QObject::connect(view, &QWebEngineView::urlChanged, this,
                   [this, tab_ptr](const QUrl& new_url) {
                     tab_ptr->url = new_url.toString();
                     if (tab_ptr->title.isEmpty() || tab_ptr->title == tab_ptr->url) {
                       tab_ptr->title = tab_ptr->url;
                     }
                     notifyTabsChanged();
                     ApplyRulesToView(tab_ptr->view, new_url);
                   });
  QObject::connect(page, &QWebEnginePage::windowCloseRequested, this,
                   [this, tab_id = tab->id]() { closeById(tab_id); });

  if (stack_) {
    view->setParent(stack_);
    stack_->addWidget(view);
    view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    view->resize(stack_->size());
  }

  if (tab->active) {
    for (auto& existing : tabs_) {
      existing->active = false;
    }
  }

  size_t insert_index = tabs_.size();
  if (!append_to_end && !tabs_.empty()) {
    int target_index = prior_active_index;
    if (target_index < 0) {
      target_index = static_cast<int>(tabs_.size()) - 1;
    }
    insert_index = static_cast<size_t>(target_index + 1);
    if (insert_index > tabs_.size()) {
      insert_index = tabs_.size();
    }
  }
  const auto insert_pos =
      static_cast<std::vector<std::unique_ptr<TabEntry>>::difference_type>(
          insert_index);
  tabs_.insert(tabs_.begin() + insert_pos, std::move(tab));
  ApplyRulesToView(view, url);
  if (!url.isEmpty()) {
    view->setUrl(url);
  }
  EnsureEvalBridge(tab_ptr);
  applyActiveState();
  notifyTabsChanged();
  if (tab_ptr->active && tab_ptr->view) {
    tab_ptr->view->setFocus();
  }
  return tab_ptr->id;
}

void TabManager::EnsureEvalBridge(TabEntry* tab) {
  if (!tab || tab->eval_bridge || !tab->view) {
    return;
  }
  QWebEnginePage* page = tab->view->page();
  if (!page) {
    return;
  }
  InstallEvalHelperScript(page);

  auto bridge = std::make_unique<JsEvalBridge>();
  const int tab_id = tab->id;
  QObject::connect(bridge.get(), &JsEvalBridge::Ready, tab->view,
                   [this, tab_id]() {
                     if (TabEntry* entry = findById(tab_id)) {
                       entry->eval_bridge_ready = true;
                     }
                   });
  QObject::connect(page, &QWebEnginePage::loadStarted, tab->view,
                   [this, tab_id]() {
                     if (TabEntry* entry = findById(tab_id)) {
                       entry->eval_bridge_ready = false;
                     }
                   });

  auto channel = std::make_unique<QWebChannel>(tab->view);
  channel->registerObject(QStringLiteral("rethreadEvalBridge"),
                          bridge.get());
  page->setWebChannel(channel.get());
  tab->eval_bridge_ready = false;
  tab->eval_bridge = std::move(bridge);
  tab->eval_channel = std::move(channel);
}

bool TabManager::activateTab(int id) {
  TabEntry* target = findById(id);
  if (!target || target->active) {
    return target != nullptr;
  }
  for (auto& tab : tabs_) {
    tab->active = (tab.get() == target);
  }
  applyActiveState();
  notifyTabsChanged();
  return true;
}

bool TabManager::cycleActiveTab(int delta) {
  if (tabs_.empty() || delta == 0) {
    return !tabs_.empty();
  }
  int current = activeIndex();
  if (current < 0) {
    current = 0;
  }
  const int count = static_cast<int>(tabs_.size());
  int next = (current + delta) % count;
  if (next < 0) {
    next += count;
  }
  if (next == current) {
    return true;
  }
  return activateTab(tabs_[next]->id);
}

QList<TabManager::TabSnapshot> TabManager::snapshot() const {
  QList<TabSnapshot> result;
  result.reserve(static_cast<int>(tabs_.size()));
  for (const auto& tab : tabs_) {
    TabSnapshot snap;
    snap.id = tab->id;
    snap.url = tab->url;
    snap.title = TabTitleOrUrl(tab->title, tab->url);
    snap.active = tab->active;
    result.append(snap);
  }
  return result;
}

bool TabManager::SwapTabs(int first_index, int second_index) {
  if (first_index < 0 || second_index < 0 ||
      first_index >= static_cast<int>(tabs_.size()) ||
      second_index >= static_cast<int>(tabs_.size())) {
    return false;
  }
  if (first_index == second_index) {
    return true;
  }
  std::swap(tabs_[first_index], tabs_[second_index]);
  applyActiveState();
  notifyTabsChanged();
  return true;
}

bool TabManager::closeTabAtIndex(int index) {
  if (index < 0 || index >= static_cast<int>(tabs_.size())) {
    return false;
  }
  auto it = tabs_.begin() + index;
  TabEntry* tab_to_close = it->get();
  WebView* view_to_close = tab_to_close ? tab_to_close->view : nullptr;
  const bool was_active = tab_to_close && tab_to_close->active;

  if (was_active && tabs_.size() > 1) {
    int replacement_index = (index + 1 < static_cast<int>(tabs_.size()))
                                ? index + 1
                                : index - 1;
    for (size_t i = 0; i < tabs_.size(); ++i) {
      tabs_[i]->active = (static_cast<int>(i) == replacement_index);
    }
    applyActiveState();
  }

  if (stack_ && view_to_close) {
    stack_->removeWidget(view_to_close);
  }
  if (view_to_close) {
    view_to_close->deleteLater();
  }

  tabs_.erase(it);

  if (tabs_.empty()) {
    emit allTabsClosed();
  } else {
    applyActiveState();
  }
  notifyTabsChanged();
  return true;
}

bool TabManager::closeActiveTab() {
  const int index = activeIndex();
  if (index < 0) {
    return false;
  }
  return closeTabAtIndex(index);
}

void TabManager::closeAllTabs() {
  while (!tabs_.empty()) {
    closeTabAtIndex(0);
  }
}

bool TabManager::historyBack() {
  QWebEngineView* view = activeView();
  if (!view) {
    return false;
  }
  QWebEngineHistory* history = view->history();
  if (!history || !history->canGoBack()) {
    return false;
  }
  history->back();
  return true;
}

bool TabManager::historyForward() {
  QWebEngineView* view = activeView();
  if (!view) {
    return false;
  }
  QWebEngineHistory* history = view->history();
  if (!history || !history->canGoForward()) {
    return false;
  }
  history->forward();
  return true;
}

QWebEngineView* TabManager::activeView() const {
  for (const auto& tab : tabs_) {
    if (tab->active) {
      return tab->view;
    }
  }
  return nullptr;
}

QWebEngineView* TabManager::createPopupTab() {
  int id = openTab(QUrl(), true);
  if (id <= 0) {
    return nullptr;
  }
  TabEntry* tab = findById(id);
  return tab ? tab->view : nullptr;
}

TabManager::TabEntry* TabManager::findById(int id) {
  for (auto& tab : tabs_) {
    if (tab->id == id) {
      return tab.get();
    }
  }
  return nullptr;
}

const TabManager::TabEntry* TabManager::findById(int id) const {
  for (const auto& tab : tabs_) {
    if (tab->id == id) {
      return tab.get();
    }
  }
  return nullptr;
}

int TabManager::activeIndex() const {
  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (tabs_[i]->active) {
      return static_cast<int>(i);
    }
  }
  return tabs_.empty() ? -1 : 0;
}

void TabManager::applyActiveState() {
  if (!stack_) {
    return;
  }
  for (const auto& tab : tabs_) {
    if (tab->view) {
      tab->view->setVisible(tab->active);
    }
    if (tab->active && tab->view) {
      stack_->setCurrentWidget(tab->view);
      tab->view->setFocus();
    }
  }
}

void TabManager::notifyTabsChanged() {
  emit tabsChanged(snapshot());
}

int TabManager::nextTabId() {
  return next_tab_id_++;
}

void TabManager::ApplyRulesToView(WebView* view, const QUrl& url) const {
  if (!view || !view->page() || !view->page()->settings()) {
    return;
  }
  const bool disable =
      rules_manager_ && rules_manager_->ShouldDisableJavaScript(url);
  view->page()->settings()->setAttribute(
      QWebEngineSettings::JavascriptEnabled, !disable);
}

void TabManager::ApplyRulesToAllTabs() const {
  if (tabs_.empty()) {
    return;
  }
  for (const auto& tab : tabs_) {
    if (tab->view) {
      ApplyRulesToView(tab->view, tab->view->url());
    }
  }
}

bool TabManager::OpenDevToolsForActiveTab() {
  QWebEngineView* view = activeView();
  if (!view || !view->page()) {
    return false;
  }
  QWebEnginePage* inspected_page = view->page();
  auto it = devtools_windows_.find(inspected_page);
  if (it != devtools_windows_.end()) {
    if (auto existing = it->second.view) {
      existing->raise();
      existing->activateWindow();
      return true;
    }
    devtools_windows_.erase(it);
  }

  auto* devtools_page = new QWebEnginePage(profile_, this);
  inspected_page->setDevToolsPage(devtools_page);
  auto* devtools_view = new QWebEngineView();
  devtools_view->setAttribute(Qt::WA_DeleteOnClose);
  devtools_view->setWindowTitle(
      QStringLiteral("DevTools - %1").arg(view->title()));
  devtools_view->setPage(devtools_page);
  devtools_view->resize(900, 700);
  devtools_view->show();
  DevToolsWindow record;
  record.view = devtools_view;
  record.inspected_page = inspected_page;
  record.devtools_page = devtools_page;
  devtools_windows_[inspected_page] = record;
  QObject::connect(
      devtools_page, &QWebEnginePage::windowCloseRequested, this,
      [this, inspected_page]() { CloseDevTools(inspected_page, true); });
  QObject::connect(
      devtools_view, &QObject::destroyed, this,
      [this, inspected_page]() { CloseDevTools(inspected_page, false); });
  QObject::connect(
      inspected_page, &QObject::destroyed, this,
      [this, inspected_page]() { CloseDevTools(inspected_page, true); });
  return true;
}

void TabManager::CloseDevTools(QWebEnginePage* page, bool close_view) {
  if (!page) {
    return;
  }
  auto it = devtools_windows_.find(page);
  if (it == devtools_windows_.end()) {
    return;
  }
  DevToolsWindow entry = it->second;
  devtools_windows_.erase(it);
  if (entry.inspected_page) {
    entry.inspected_page->setDevToolsPage(nullptr);
  }
  if (entry.devtools_page) {
    entry.devtools_page->deleteLater();
  }
  if (close_view && entry.view) {
    entry.view->close();
  }
}

bool TabManager::EvaluateJavaScript(const QString& script,
                                    int tab_id,
                                    int tab_index,
                                    QVariant* result,
                                    QString* error_message) {
  if (tabs_.empty()) {
    if (error_message) {
      *error_message = QStringLiteral("no tabs available");
    }
    return false;
  }
  TabEntry* target = nullptr;
  if (tab_id > 0) {
    target = findById(tab_id);
    if (!target) {
      if (error_message) {
        *error_message = QStringLiteral("unknown tab id");
      }
      return false;
    }
  } else if (tab_index > 0) {
    const int zero_based = tab_index - 1;
    if (zero_based < 0 ||
        zero_based >= static_cast<int>(tabs_.size())) {
      if (error_message) {
        *error_message = QStringLiteral("tab index out of range");
      }
      return false;
    }
    target = tabs_[static_cast<size_t>(zero_based)].get();
  } else {
    for (const auto& tab : tabs_) {
      if (tab->active) {
        target = tab.get();
        break;
      }
    }
    if (!target) {
      target = tabs_.front().get();
    }
  }

  if (!target || !target->view || !target->view->page()) {
    if (error_message) {
      *error_message = QStringLiteral("tab has no page");
    }
    return false;
  }

  EnsureEvalBridge(target);
  if (!target->eval_bridge) {
    if (error_message) {
      *error_message = QStringLiteral("eval bridge unavailable");
    }
    return false;
  }

  if (!target->eval_bridge_ready) {
    QEventLoop ready_loop;
    QMetaObject::Connection ready_conn = QObject::connect(
        target->eval_bridge.get(), &JsEvalBridge::Ready, &ready_loop,
        &QEventLoop::quit);
    QTimer ready_timer;
    ready_timer.setSingleShot(true);
    QObject::connect(&ready_timer, &QTimer::timeout, &ready_loop,
                     &QEventLoop::quit);
    ready_timer.start(kEvalReadyTimeoutMs);
    ready_loop.exec();
    QObject::disconnect(ready_conn);
    if (!target->eval_bridge_ready) {
      if (error_message) {
        *error_message = QStringLiteral("eval environment unavailable");
      }
      return false;
    }
  }

  const int request_id = target->next_eval_request_id++;
  QEventLoop loop;
  QVariant callback_result;
  QString callback_error;
  bool completed = false;
  bool success = false;

  QPointer<WebView> view_guard(target->view);
  QObject::connect(target->eval_bridge.get(), &QObject::destroyed, &loop,
                   &QEventLoop::quit);
  if (view_guard) {
    QObject::connect(view_guard.data(), &QObject::destroyed, &loop,
                     &QEventLoop::quit);
  }

  QMetaObject::Connection completion_conn =
      QObject::connect(target->eval_bridge.get(), &JsEvalBridge::EvalCompleted,
                       &loop,
                       [&loop, &completed, &success, &callback_result,
                        &callback_error, request_id](int completed_id,
                                                     bool completed_success,
                                                     const QVariant& value,
                                                     const QString& error) {
                         if (completed || completed_id != request_id) {
                           return;
                         }
                         completed = true;
                         success = completed_success;
                         if (completed_success) {
                           callback_result = value;
                         } else {
                           callback_error = error;
                         }
                         loop.quit();
                       });

  emit target->eval_bridge->EvalRequested(request_id, script);
  loop.exec();
  QObject::disconnect(completion_conn);

  if (!completed) {
    if (error_message) {
      if (!view_guard || view_guard.isNull()) {
        *error_message = QStringLiteral("tab closed during evaluation");
      } else {
        *error_message = QStringLiteral("script did not produce a result");
      }
    }
    return false;
  }

  if (success) {
    if (result) {
      *result = callback_result;
    }
    return true;
  }

  if (error_message) {
    QString message = callback_error.trimmed();
    if (message.isEmpty()) {
      message = QStringLiteral("failed to evaluate script");
    }
    *error_message = message;
  }
  return false;
}

bool TabManager::closeById(int id) {
  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (tabs_[i]->id == id) {
      return closeTabAtIndex(static_cast<int>(i));
    }
  }
  return false;
}

}  // namespace rethread
