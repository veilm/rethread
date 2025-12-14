#include "browser/tab_strip_overlay.h"

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
  Rebuild();
}

QSize TabStripOverlay::sizeHint() const {
  return QFrame::sizeHint();
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

  for (int i = 0; i < entries_.size(); ++i) {
    const auto& entry = entries_[i];
    auto* label = new QLabel(QStringLiteral("[%1] %2")
                                 .arg(i + 1)
                                 .arg(entry.title), this);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(QStringLiteral("font-size: 18px;"));
    QPalette pal = label->palette();
    pal.setColor(QPalette::WindowText,
                 entry.active ? kActiveColor : kInactiveColor);
    label->setPalette(pal);
    layout()->addWidget(label);
  }
  updateGeometry();
}

}  // namespace rethread
