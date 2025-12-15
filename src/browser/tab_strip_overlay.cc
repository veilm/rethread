#include "browser/tab_strip_overlay.h"

#include <algorithm>

#include <QColor>
#include <QLabel>
#include <QPalette>
#include <QVBoxLayout>

namespace rethread {
namespace {
constexpr int kPadding = 12;
constexpr int kRowSpacing = 6;
constexpr int kActiveAlpha = 255;
constexpr int kInactiveAlpha = 180;
constexpr int kMaxDisplayLength = 80;
const QColor kBackgroundColor(32, 32, 32, 224);
const QColor kActiveColor(255, 255, 255);
const QColor kInactiveColor(200, 200, 200);
}  // namespace

TabStripOverlay::TabStripOverlay(QWidget* parent)
    : QFrame(parent) {
  setFrameShape(QFrame::Panel);
  setFrameShadow(QFrame::Raised);
  setAutoFillBackground(true);
  setAttribute(Qt::WA_ShowWithoutActivating);
  setAttribute(Qt::WA_TransparentForMouseEvents, true);
  QPalette pal = palette();
  pal.setColor(QPalette::Window, kBackgroundColor);
  setPalette(pal);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(kPadding, kPadding, kPadding, kPadding);
  layout->setSpacing(kRowSpacing);
}

void TabStripOverlay::SetTabs(const QList<Entry>& entries) {
  entries_ = entries;
  if (!showing_custom_message_) {
    Rebuild();
  }
}

void TabStripOverlay::SetCustomMessage(const QStringList& lines) {
  custom_lines_ = lines;
  showing_custom_message_ = true;
  Rebuild();
}

void TabStripOverlay::ClearCustomMessage() {
  showing_custom_message_ = false;
  custom_lines_.clear();
  Rebuild();
}

QSize TabStripOverlay::sizeHint() const {
  return QFrame::sizeHint();
}

QString TabStripOverlay::TruncateForDisplay(const QString& text) const {
  if (text.size() <= kMaxDisplayLength) {
    return text;
  }
  const int prefix = std::max(0, kMaxDisplayLength - 3);
  return text.left(prefix) + QStringLiteral("...");
}

void TabStripOverlay::Rebuild() {
  if (!layout()) {
    return;
  }
  QLayoutItem* child;
  while ((child = layout()->takeAt(0)) != nullptr) {
    if (auto widget = child->widget()) {
      widget->deleteLater();
    }
    delete child;
  }

  if (showing_custom_message_) {
    for (const QString& line : custom_lines_) {
      const QString trimmed = line.trimmed();
      if (trimmed.isEmpty()) {
        continue;
      }
      const QString display = TruncateForDisplay(trimmed);
      auto* label = new QLabel(display, this);
      label->setAlignment(Qt::AlignCenter);
      label->setStyleSheet(QStringLiteral("font-size: 18px;"));
      if (display != trimmed) {
        label->setToolTip(trimmed);
      }
      QPalette pal = label->palette();
      pal.setColor(QPalette::WindowText, kActiveColor);
      label->setPalette(pal);
      layout()->addWidget(label);
    }
  } else {
    for (int i = 0; i < entries_.size(); ++i) {
      const auto& entry = entries_[i];
      const QString raw_text = QStringLiteral("[%1] %2")
                                   .arg(i + 1)
                                   .arg(entry.title);
      const QString display = TruncateForDisplay(raw_text);
      auto* label = new QLabel(display, this);
      label->setAlignment(Qt::AlignCenter);
      label->setStyleSheet(QStringLiteral("font-size: 18px;"));
      if (display != raw_text) {
        label->setToolTip(raw_text);
      }
      QPalette pal = label->palette();
      pal.setColor(QPalette::WindowText,
                   entry.active ? kActiveColor : kInactiveColor);
      label->setPalette(pal);
      layout()->addWidget(label);
    }
  }
  updateGeometry();
}

}  // namespace rethread
