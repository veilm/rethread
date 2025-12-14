#include "browser/context_menu_binding_manager.h"

#include <string>

#include "common/debug_log.h"

namespace rethread {

ContextMenuBindingManager::ContextMenuBindingManager(QObject* parent)
    : QObject(parent) {}

void ContextMenuBindingManager::SetBinding(const QString& command) {
  const QString trimmed = command.trimmed();
  binding_command_ = trimmed;
  AppendDebugLog("Updated context menu binding to \"" +
                 binding_command_.toStdString() + "\"");
}

void ContextMenuBindingManager::ClearBinding() {
  const bool had_binding = !binding_command_.isEmpty();
  binding_command_.clear();
  if (had_binding) {
    AppendDebugLog("Cleared context menu binding");
  }
}

bool ContextMenuBindingManager::HasBinding() const {
  return !binding_command_.trimmed().isEmpty();
}

QString ContextMenuBindingManager::binding() const {
  return binding_command_;
}

}  // namespace rethread
