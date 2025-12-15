#include "browser/tab_strip_controller.h"

#include <algorithm>

namespace rethread {

TabStripController::TabStripController(TabStripOverlay* overlay,
                                       QObject* parent)
    : QObject(parent), overlay_(overlay) {
  hide_timer_.setSingleShot(true);
  connect(&hide_timer_, &QTimer::timeout, this, [this]() {
    visible_ = false;
    ClearCustomMessage();
    ApplyVisibility(false);
  });
  if (overlay_) {
    overlay_->hide();
  }
}

void TabStripController::SetTabs(
    const QList<TabManager::TabSnapshot>& tabs) {
  if (!overlay_) {
    return;
  }
  current_entries_.clear();
  current_entries_.reserve(tabs.size());
  for (const auto& tab : tabs) {
    TabStripOverlay::Entry entry;
    entry.title = tab.title;
    entry.active = tab.active;
    current_entries_.append(entry);
  }
  overlay_->SetTabs(current_entries_);
}

void TabStripController::Show() {
  CancelPendingHide();
  ClearCustomMessage();
  visible_ = true;
  ApplyVisibility(true);
}

void TabStripController::Hide() {
  CancelPendingHide();
  ClearCustomMessage();
  visible_ = false;
  ApplyVisibility(false);
}

void TabStripController::Toggle() {
  CancelPendingHide();
  if (!visible_) {
    ClearCustomMessage();
  }
  visible_ = !visible_;
  ApplyVisibility(visible_);
}

void TabStripController::Peek(int milliseconds) {
  CancelPendingHide();
  ClearCustomMessage();
  visible_ = true;
  ApplyVisibility(true);
  const int duration = std::max(0, milliseconds);
  if (duration == 0) {
    Hide();
    return;
  }
  hide_timer_.start(duration);
}

void TabStripController::ShowMessage(const QStringList& lines, int milliseconds) {
  if (!overlay_) {
    return;
  }
  QStringList filtered;
  for (const QString& line : lines) {
    const QString trimmed = line.trimmed();
    if (!trimmed.isEmpty()) {
      filtered.append(trimmed);
    }
  }
  if (filtered.isEmpty()) {
    return;
  }
  CancelPendingHide();
  custom_message_lines_ = filtered;
  showing_custom_message_ = true;
  overlay_->SetCustomMessage(custom_message_lines_);
  visible_ = true;
  overlay_->show();
  overlay_->raise();
  const int duration = std::max(0, milliseconds);
  if (duration > 0) {
    hide_timer_.start(duration);
  }
}

void TabStripController::ApplyVisibility(bool visible) {
  if (!overlay_) {
    return;
  }
  if (visible) {
    if (showing_custom_message_) {
      overlay_->SetCustomMessage(custom_message_lines_);
    } else {
      overlay_->SetTabs(current_entries_);
    }
    overlay_->show();
    overlay_->raise();
  } else {
    overlay_->hide();
  }
}

void TabStripController::CancelPendingHide() {
  if (hide_timer_.isActive()) {
    hide_timer_.stop();
  }
}

void TabStripController::ClearCustomMessage() {
  if (!showing_custom_message_) {
    return;
  }
  showing_custom_message_ = false;
  custom_message_lines_.clear();
  if (overlay_) {
    overlay_->ClearCustomMessage();
  }
}

}  // namespace rethread
