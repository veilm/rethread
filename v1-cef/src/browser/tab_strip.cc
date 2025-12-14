#include "browser/tab_strip.h"

#include "include/views/cef_layout.h"

#include "common/debug_log.h"

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

CefRefPtr<CefPanel> TabStripView::GetPanel() const {
  return panel_;
}

CefSize TabStripView::GetPreferredSize() const {
  if (!panel_) {
    return CefSize();
  }
  return panel_->GetPreferredSize();
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
    CefRefPtr<CefLabelButton> row =
        CefLabelButton::CreateLabelButton(this, CefString());
    row->SetBackgroundColor(tab.active ? CefColorSetARGB(255, 80, 80, 80)
                                       : CefColorSetARGB(180, 45, 45, 45));
    row->SetEnabledTextColors(tab.active ? CefColorSetARGB(255, 255, 255, 255)
                                         : CefColorSetARGB(255, 200, 200, 200));
    std::string text = "[" + std::to_string(i + 1) + "] " + tab.title;
    CefString label_text;
    label_text.FromString(text);
    row->SetText(label_text);
    panel->AddChildView(row);
    box_layout->SetFlexForView(row, 0);
    AppendDebugLog("TabStrip row " + std::to_string(i + 1) + ": " + text);
  }

  panel->Layout();
}

}  // namespace rethread
