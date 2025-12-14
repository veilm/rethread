#ifndef RETHREAD_BROWSER_KEY_BINDING_MANAGER_H_
#define RETHREAD_BROWSER_KEY_BINDING_MANAGER_H_

#include <optional>
#include <string>
#include <vector>

#include "include/cef_base.h"
#include "include/internal/cef_types_wrappers.h"

namespace rethread {

class KeyBindingManager {
 public:
  struct Binding {
    bool alt = false;
    bool ctrl = false;
    bool shift = false;
    bool command = false;
    bool consume = true;
    std::string key;
    std::string command_line;
  };

  static KeyBindingManager* Get();

  bool AddBinding(Binding binding);
  std::optional<bool> HandleKeyEvent(const CefKeyEvent& event);

 private:
  KeyBindingManager();

  std::string NormalizeKey(const std::string& key) const;
  std::string ExtractKeyLabel(const CefKeyEvent& event) const;
  void ExecuteCommand(const std::string& command) const;

  std::vector<Binding> bindings_;

  KeyBindingManager(const KeyBindingManager&) = delete;
  KeyBindingManager& operator=(const KeyBindingManager&) = delete;
};

}  // namespace rethread

#endif  // RETHREAD_BROWSER_KEY_BINDING_MANAGER_H_
