#ifndef RETHREAD_BROWSER_COMMAND_DISPATCHER_H_
#define RETHREAD_BROWSER_COMMAND_DISPATCHER_H_

#include <QString>

namespace rethread {

class KeyBindingManager;
class ContextMenuBindingManager;
class RulesManager;
class TabManager;
class TabStripController;

class CommandDispatcher {
 public:
  CommandDispatcher(TabManager* tab_manager,
                    KeyBindingManager* key_binding_manager,
                    ContextMenuBindingManager* context_menu_binding_manager,
                    RulesManager* rules_manager,
                    TabStripController* tab_strip_controller);

  QString Execute(const QString& command) const;

 private:
  QString HandleList() const;
  QString HandleSwitch(int id) const;
  QString HandleCycle(int delta) const;
  QString HandleClose(const QString& index_text) const;
  QString HandleOpen(const QString& url) const;
  QString HandleHistoryBack() const;
  QString HandleHistoryForward() const;
  QString HandleBind(const QString& args) const;
  QString HandleUnbind(const QString& args) const;
  QString HandleTabStrip(const QString& args) const;
  QString HandleEval(const QString& args) const;
  QString HandleRules(const QString& args) const;

  TabManager* tab_manager_;
  KeyBindingManager* key_binding_manager_;
  ContextMenuBindingManager* context_menu_binding_manager_;
  RulesManager* rules_manager_;
  TabStripController* tab_strip_controller_;
};

}  // namespace rethread

#endif  // RETHREAD_BROWSER_COMMAND_DISPATCHER_H_
