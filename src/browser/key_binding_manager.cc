#include "browser/key_binding_manager.h"

#include <algorithm>
#include <string>

#include <QApplication>
#include <QChar>
#include <QEvent>
#include <QKeyEvent>
#include <QProcess>

#include "common/debug_log.h"

namespace rethread {

KeyBindingManager::KeyBindingManager(QObject* parent) : QObject(parent) {
  if (qApp) {
    qApp->installEventFilter(this);
  }
}

bool KeyBindingManager::AddBinding(Binding binding) {
  binding.key = NormalizeKey(binding.key);
  binding.command_line = binding.command_line.trimmed();
  if (binding.key.isEmpty() || binding.command_line.isEmpty()) {
    return false;
  }
  bindings_.push_back(binding);
  AppendDebugLog("Added key binding key=" + binding.key.toStdString() +
                 " alt=" + std::to_string(binding.alt) +
                 " ctrl=" + std::to_string(binding.ctrl) +
                 " shift=" + std::to_string(binding.shift) +
                 " command=" + std::to_string(binding.command) +
                 " consume=" + std::to_string(binding.consume) +
                 " command_line=\"" + binding.command_line.toStdString() + "\"");
  return true;
}

bool KeyBindingManager::RemoveBinding(const Binding& binding) {
  if (binding.key.trimmed().isEmpty()) {
    return false;
  }
  const QString normalized = NormalizeKey(binding.key);
  const size_t before = bindings_.size();
  bindings_.erase(std::remove_if(bindings_.begin(), bindings_.end(),
                                 [&](const Binding& existing) {
                                   return existing.key == normalized &&
                                          existing.alt == binding.alt &&
                                          existing.ctrl == binding.ctrl &&
                                          existing.shift == binding.shift &&
                                          existing.command == binding.command;
                                 }),
                  bindings_.end());
  const size_t removed = before - bindings_.size();
  AppendDebugLog("Removed " + std::to_string(removed) +
                 " key binding(s) for key=" + normalized.toStdString());
  return removed > 0;
}

bool KeyBindingManager::eventFilter(QObject* watched, QEvent* event) {
  if (event->type() == QEvent::KeyPress) {
    auto* key_event = static_cast<QKeyEvent*>(event);
    if (key_event->isAutoRepeat()) {
      return QObject::eventFilter(watched, event);
    }
    auto result = HandleKeyEvent(key_event);
    if (result.has_value()) {
      return result.value();
    }
  }
  return QObject::eventFilter(watched, event);
}

std::optional<bool> KeyBindingManager::HandleKeyEvent(QKeyEvent* event) {
  if (!event) {
    return std::nullopt;
  }
  const QString label = ExtractKeyLabel(event);
  if (label.isEmpty()) {
    return std::nullopt;
  }
  const bool alt_down = event->modifiers().testFlag(Qt::AltModifier);
  const bool ctrl_down = event->modifiers().testFlag(Qt::ControlModifier);
  const bool shift_down = event->modifiers().testFlag(Qt::ShiftModifier);
  const bool command_down = event->modifiers().testFlag(Qt::MetaModifier);

  for (auto it = bindings_.rbegin(); it != bindings_.rend(); ++it) {
    const Binding& binding = *it;
    if (binding.key != label) {
      continue;
    }
    if (binding.alt != alt_down ||
        binding.ctrl != ctrl_down ||
        binding.shift != shift_down ||
        binding.command != command_down) {
      continue;
    }
    ExecuteCommand(binding.command_line);
    return binding.consume;
  }
  return std::nullopt;
}

QString KeyBindingManager::NormalizeKey(const QString& key) const {
  QString lowered = key.trimmed().toLower();
  return lowered;
}

QString KeyBindingManager::ExtractKeyLabel(QKeyEvent* event) const {
  QString text = event->text();
  if (!text.isEmpty() && text.at(0).isPrint()) {
    return NormalizeKey(text.left(1));
  }
  int key = event->key();
  if (key >= Qt::Key_A && key <= Qt::Key_Z) {
    const QChar letter('a' + (key - Qt::Key_A));
    return QString(letter);
  }
  if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {
    return QStringLiteral("tab");
  }
  if (key == Qt::Key_Left) {
    return QStringLiteral("left");
  }
  if (key == Qt::Key_Right) {
    return QStringLiteral("right");
  }
  return QString();
}

void KeyBindingManager::ExecuteCommand(const QString& command) const {
  if (command.trimmed().isEmpty()) {
    return;
  }
  QProcess::startDetached(QStringLiteral("/bin/sh"),
                          {QStringLiteral("-c"), command});
}

}  // namespace rethread
