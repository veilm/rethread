#include "browser/tab_strip.h"

#include "include/views/cef_layout.h"

namespace rethread {

TabStripView::TabStripView() = default;

void TabStripView::Initialize() {
  if (panel_) {
    return;
  }
  panel_ = CefPanel::CreatePanel(this);
  CefBoxLayoutSettings settings;
  settings.horizontal = false;
  settings.inside_border_insets = CefInsets(4, 8, 4, 8);
  panel_->SetToBoxLayout(settings);
}

void TabStripView::SetTabs(const std::vector<Tab>& tabs) {
  tabs_ = tabs;
  UpdateContent();
}

void TabStripView::UpdateContent() {
  CefRefPtr<CefPanel> panel = panel_;
  if (!panel) {
    return;
  }

  panel->RemoveAllChildViews();

  CefRefPtr<CefLayout> layout = panel->GetLayout();
  CefRefPtr<CefBoxLayout> box_layout = layout ? layout->AsBoxLayout() : nullptr;
  if (!box_layout) {
    return;
  }

  for (size_t i = 0; i < tabs_.size(); ++i) {
    const auto& tab = tabs_[i];
    CefRefPtr<CefTextfield> row = CefTextfield::CreateTextfield(nullptr);
    row->SetReadOnly(true);
    row->SetBackgroundColor(tab.active ? CefColorSetARGB(255, 80, 80, 80)
                                       : CefColorSetARGB(180, 45, 45, 45));
    row->SetTextColor(tab.active ? CefColorSetARGB(255, 255, 255, 255)
                                 : CefColorSetARGB(255, 200, 200, 200));
    row->SetText("[" + std::to_string(i + 1) + "] " + tab.title);
    panel->AddChildView(row);
    box_layout->SetFlexForView(row, 0);
  }
}

}  // namespace rethread
