#include "browser/key_binding_manager.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <thread>

#include "common/debug_log.h"
#include "include/wrapper/cef_helpers.h"

namespace rethread {
namespace {

KeyBindingManager* g_binding_manager = nullptr;

bool IsPrintable(int value) {
  return value >= 32 && value <= 126;
}

}  // namespace

KeyBindingManager* KeyBindingManager::Get() {
  if (!g_binding_manager) {
    g_binding_manager = new KeyBindingManager();
  }
  return g_binding_manager;
}

KeyBindingManager::KeyBindingManager() = default;

std::string KeyBindingManager::NormalizeKey(const std::string& key) const {
  std::string lower = key;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return lower;
}

bool KeyBindingManager::AddBinding(Binding binding) {
  CEF_REQUIRE_UI_THREAD();
  if (binding.key.empty() || binding.command_line.empty()) {
    return false;
  }
  binding.key = NormalizeKey(binding.key);
  bindings_.push_back(binding);
  AppendDebugLog("Added key binding key=" + binding.key +
                 " alt=" + std::to_string(binding.alt) +
                 " ctrl=" + std::to_string(binding.ctrl) +
                 " shift=" + std::to_string(binding.shift) +
                 " command=" + std::to_string(binding.command) +
                 " consume=" + std::to_string(binding.consume) +
                 " command_line=\"" + binding.command_line + "\"");
  return true;
}

bool KeyBindingManager::RemoveBinding(const Binding& binding) {
  CEF_REQUIRE_UI_THREAD();
  if (binding.key.empty()) {
    return false;
  }
  Binding normalized = binding;
  normalized.key = NormalizeKey(binding.key);

  const size_t before = bindings_.size();
  bindings_.erase(
      std::remove_if(bindings_.begin(), bindings_.end(),
                     [&](const Binding& existing) {
                       return existing.key == normalized.key &&
                              existing.alt == normalized.alt &&
                              existing.ctrl == normalized.ctrl &&
                              existing.shift == normalized.shift &&
                              existing.command == normalized.command;
                     }),
      bindings_.end());
  const size_t removed = before - bindings_.size();
  AppendDebugLog("Removed " + std::to_string(removed) +
                 " key binding(s) for key=" + normalized.key +
                 " alt=" + std::to_string(normalized.alt) +
                 " ctrl=" + std::to_string(normalized.ctrl) +
                 " shift=" + std::to_string(normalized.shift) +
                 " command=" + std::to_string(normalized.command));
  return true;
}

std::string KeyBindingManager::ExtractKeyLabel(const CefKeyEvent& event) const {
  if (IsPrintable(event.unmodified_character)) {
    std::string label(1, static_cast<char>(event.unmodified_character));
    return NormalizeKey(label);
  }
  if (IsPrintable(event.character)) {
    std::string label(1, static_cast<char>(event.character));
    return NormalizeKey(label);
  }

  if (event.windows_key_code >= 65 && event.windows_key_code <= 90) {
    std::string label(1, static_cast<char>('a' + event.windows_key_code - 65));
    return label;
  }

  if (event.windows_key_code == 9) {
    return "tab";
  }

  if ((event.modifiers & EVENTFLAG_CONTROL_DOWN) != 0 ||
      (event.modifiers & EVENTFLAG_COMMAND_DOWN) != 0) {
    const int value = event.character;
    if (value >= 1 && value <= 26) {
      std::string label(1, static_cast<char>('a' + value - 1));
      return label;
    }
  }

  return std::string();
}

void KeyBindingManager::ExecuteCommand(const std::string& command) const {
  if (command.empty()) {
    return;
  }
  std::string shell_command = command;
  std::thread([shell_command]() {
    int status = std::system(shell_command.c_str());
    if (status != 0) {
      AppendDebugLog("Key binding command exited with status " +
                     std::to_string(status));
    }
  }).detach();
}

std::optional<bool> KeyBindingManager::HandleKeyEvent(
    const CefKeyEvent& event) {
  CEF_REQUIRE_UI_THREAD();
  std::string label = ExtractKeyLabel(event);
  if (label.empty()) {
    return std::nullopt;
  }

  const bool alt_down = (event.modifiers & EVENTFLAG_ALT_DOWN) != 0;
  const bool ctrl_down = (event.modifiers & EVENTFLAG_CONTROL_DOWN) != 0;
  const bool shift_down = (event.modifiers & EVENTFLAG_SHIFT_DOWN) != 0;
  const bool command_down = (event.modifiers & EVENTFLAG_COMMAND_DOWN) != 0;

  for (auto it = bindings_.rbegin(); it != bindings_.rend(); ++it) {
    const Binding& binding = *it;
    if (binding.key != label) {
      continue;
    }
    if (binding.alt != alt_down) {
      continue;
    }
    if (binding.ctrl != ctrl_down) {
      continue;
    }
    if (binding.shift != shift_down) {
      continue;
    }
    if (binding.command != command_down) {
      continue;
    }
    ExecuteCommand(binding.command_line);
    return binding.consume;
  }

  return std::nullopt;
}

}  // namespace rethread
