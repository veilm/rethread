#include "browser/tab_manager.h"

#include "include/views/cef_fill_layout.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"

#include "browser/tab_strip.h"

namespace rethread {
namespace {
TabManager* g_tab_manager = nullptr;
}

TabManager* TabManager::Get() {
  if (!g_tab_manager) {
    g_tab_manager = new TabManager();
  }
  return g_tab_manager;
}

TabManager::TabManager() = default;

void TabManager::Initialize(const InitParams& params) {
  CEF_REQUIRE_UI_THREAD();

  client_ = params.client;
  request_context_ = params.request_context;
  popup_delegate_ = params.popup_delegate;
  background_color_ = params.background_color;
  initialized_ = true;
}

int TabManager::OpenTab(const std::string& url, bool activate) {
  CEF_REQUIRE_UI_THREAD();
  if (!initialized_ || !client_) {
    return -1;
  }

  int id = next_tab_id_++;
  CefBrowserSettings browser_settings;
  browser_settings.background_color = background_color_;

  CefRefPtr<CefBrowserView> view = CefBrowserView::CreateBrowserView(
      client_, url, browser_settings, nullptr, request_context_,
      popup_delegate_);
  if (!view) {
    return -1;
  }

  view->SetID(id);
  view->SetBackgroundColor(background_color_);
  auto tab = std::make_unique<TabEntry>();
  tab->id = id;
  tab->url = url;
  tab->title = url;

  const bool should_activate = tabs_.empty() || activate;
  tab->active = should_activate;

  if (should_activate) {
    for (auto& existing : tabs_) {
      existing->active = false;
    }
  }

  tab->view = view;
  TabEntry* tab_ptr = tab.get();
  tabs_.push_back(std::move(tab));

  AttachTabView(tab_ptr);
  ApplyActiveState();
  UpdateTabStrip();
  return id;
}

bool TabManager::ActivateTab(int id) {
  CEF_REQUIRE_UI_THREAD();
  TabEntry* target = FindTabById(id);
  if (!target) {
    return false;
  }
  if (target->active) {
    return true;
  }
  for (auto& tab : tabs_) {
    tab->active = (tab.get() == target);
  }
  ApplyActiveState();
  UpdateTabStrip();
  return true;
}

std::vector<TabManager::TabSnapshot> TabManager::GetTabs() const {
  CEF_REQUIRE_UI_THREAD();
  std::vector<TabSnapshot> snapshot;
  snapshot.reserve(tabs_.size());
  for (const auto& tab : tabs_) {
    TabSnapshot info;
    info.id = tab->id;
    info.url = tab->url;
    info.title = tab->title;
    info.active = tab->active;
    snapshot.push_back(info);
  }
  return snapshot;
}

CefRefPtr<CefBrowser> TabManager::GetActiveBrowser() const {
  CEF_REQUIRE_UI_THREAD();
  for (const auto& tab : tabs_) {
    if (tab->active && tab->view) {
      return tab->view->GetBrowser();
    }
  }
  return nullptr;
}

void TabManager::CloseAllTabs(bool force_close) {
  CEF_REQUIRE_UI_THREAD();
  for (const auto& tab : tabs_) {
    if (!tab->view) {
      continue;
    }
    CefRefPtr<CefBrowser> browser = tab->view->GetBrowser();
    if (!browser) {
      continue;
    }
    browser->GetHost()->CloseBrowser(force_close);
  }
}

void TabManager::AttachWindow(CefRefPtr<CefWindow> window) {
  CEF_REQUIRE_UI_THREAD();
  window_ = window;
  EnsureContentPanel();
  for (auto& tab : tabs_) {
    AttachTabView(tab.get());
  }
  ApplyActiveState();
}

void TabManager::DetachWindow(CefRefPtr<CefWindow> window) {
  CEF_REQUIRE_UI_THREAD();
  if (window_.get() != window.get()) {
    return;
  }

  std::vector<CefRefPtr<CefBrowser>> browsers_to_close;
  browsers_to_close.reserve(tabs_.size());
  for (const auto& tab : tabs_) {
    if (!tab->view) {
      continue;
    }
    CefRefPtr<CefBrowser> browser = tab->view->GetBrowser();
    if (browser) {
      browsers_to_close.push_back(browser);
    }
  }

  window_ = nullptr;
  content_panel_ = nullptr;
  tabs_.clear();

  for (auto& browser : browsers_to_close) {
    browser->GetHost()->CloseBrowser(true);
  }
}

void TabManager::BindTabStrip(CefRefPtr<TabStripView> tab_strip) {
  CEF_REQUIRE_UI_THREAD();
  tab_strip_ = tab_strip;
  UpdateTabStrip();
}

void TabManager::UnbindTabStrip(CefRefPtr<TabStripView> tab_strip) {
  CEF_REQUIRE_UI_THREAD();
  if (tab_strip_.get() == tab_strip.get()) {
    tab_strip_ = nullptr;
  }
}

void TabManager::UpdateBrowserTitle(CefRefPtr<CefBrowser> browser,
                                    const std::string& title) {
  CEF_REQUIRE_UI_THREAD();
  TabEntry* tab = FindTabByBrowser(browser);
  if (!tab) {
    return;
  }
  tab->title = title.empty() ? tab->url : title;
  UpdateTabStrip();
}

void TabManager::EnsureContentPanel() {
  if (content_panel_ || !window_) {
    return;
  }
  content_panel_ = CefPanel::CreatePanel(nullptr);
  content_panel_->SetToFillLayout();
  window_->AddChildView(content_panel_);
}

void TabManager::AttachTabView(TabEntry* tab) {
  if (!tab || !tab->view || !window_) {
    return;
  }
  EnsureContentPanel();
  if (!content_panel_) {
    return;
  }
  if (tab->view->GetParentView()) {
    return;
  }
  content_panel_->AddChildView(tab->view);
  tab->view->SetVisible(tab->active);
}

void TabManager::ApplyActiveState() {
  for (auto& tab : tabs_) {
    if (!tab->view) {
      continue;
    }
    tab->view->SetVisible(tab->active);
    if (tab->active && window_) {
      tab->view->RequestFocus();
    }
  }
}

void TabManager::UpdateTabStrip() const {
  if (!tab_strip_) {
    return;
  }
  std::vector<TabStripView::Tab> tabs;
  tabs.reserve(tabs_.size());
  for (const auto& tab : tabs_) {
    TabStripView::Tab entry;
    entry.title = tab->title.empty() ? tab->url : tab->title;
    entry.active = tab->active;
    tabs.push_back(entry);
  }
  tab_strip_->SetTabs(tabs);
}

TabManager::TabEntry* TabManager::FindTabById(int id) {
  for (auto& tab : tabs_) {
    if (tab->id == id) {
      return tab.get();
    }
  }
  return nullptr;
}

const TabManager::TabEntry* TabManager::FindTabById(int id) const {
  for (const auto& tab : tabs_) {
    if (tab->id == id) {
      return tab.get();
    }
  }
  return nullptr;
}

TabManager::TabEntry* TabManager::FindTabByBrowser(
    CefRefPtr<CefBrowser> browser) {
  if (!browser) {
    return nullptr;
  }
  for (auto& tab : tabs_) {
    if (!tab->view) {
      continue;
    }
    CefRefPtr<CefBrowser> tab_browser = tab->view->GetBrowser();
    if (tab_browser && tab_browser->IsSame(browser)) {
      return tab.get();
    }
  }
  return nullptr;
}

}  // namespace rethread
