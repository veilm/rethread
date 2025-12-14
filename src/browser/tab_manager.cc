#include "browser/tab_manager.h"

#include <algorithm>

#include <QCoreApplication>
#include <QStackedWidget>
#include <QUrl>
#include <QWebEngineProfile>
#include <QWebEngineView>

#include "browser/web_page.h"
#include "browser/web_view.h"

namespace rethread {
namespace {
QString TabTitleOrUrl(const QString& title, const QString& url) {
  return title.isEmpty() ? url : title;
}
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
      tab->view->setVisible(tab->active);
    }
  }
  applyActiveState();
}

void TabManager::setMenuCommand(const QString& command) {
  menu_command_ = command;
}

int TabManager::openTab(const QUrl& url, bool activate) {
  if (!profile_) {
    return -1;
  }

  auto tab = std::make_unique<TabEntry>();
  tab->id = nextTabId();
  tab->active = tabs_.empty() || activate;

  auto* view = new WebView(menu_command_, background_color_);
  auto* page = new WebPage(profile_, this);
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
                   });
  QObject::connect(page, &QWebEnginePage::windowCloseRequested, this,
                   [this, tab_id = tab->id]() { closeById(tab_id); });

  if (stack_) {
    view->setParent(stack_);
    stack_->addWidget(view);
  }

  if (tab->active) {
    for (auto& existing : tabs_) {
      existing->active = false;
    }
  }

  tabs_.push_back(std::move(tab));
  if (!url.isEmpty()) {
    view->setUrl(url);
  }
  applyActiveState();
  notifyTabsChanged();
  if (tab_ptr->active && tab_ptr->view) {
    tab_ptr->view->setFocus();
  }
  return tab_ptr->id;
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

bool TabManager::closeTabAtIndex(int index) {
  if (index < 0 || index >= static_cast<int>(tabs_.size())) {
    return false;
  }
  auto it = tabs_.begin() + index;
  const bool was_active = (*it)->active;
  WebView* view = (*it)->view;
  if (stack_ && view) {
    stack_->removeWidget(view);
  }
  if (view) {
    view->deleteLater();
  }
  tabs_.erase(it);

  if (!tabs_.empty() && was_active) {
    int new_index = index;
    if (new_index >= static_cast<int>(tabs_.size())) {
      new_index = static_cast<int>(tabs_.size()) - 1;
    }
    for (size_t i = 0; i < tabs_.size(); ++i) {
      tabs_[i]->active = (static_cast<int>(i) == new_index);
    }
  }

  applyActiveState();
  notifyTabsChanged();
  if (tabs_.empty()) {
    emit allTabsClosed();
  }
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

bool TabManager::closeById(int id) {
  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (tabs_[i]->id == id) {
      return closeTabAtIndex(static_cast<int>(i));
    }
  }
  return false;
}

}  // namespace rethread
