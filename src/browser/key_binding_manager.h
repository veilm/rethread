#ifndef RETHREAD_BROWSER_KEY_BINDING_MANAGER_H_
#define RETHREAD_BROWSER_KEY_BINDING_MANAGER_H_

#include <optional>
#include <vector>

#include <QObject>
#include <QString>

class QKeyEvent;

namespace rethread {

class KeyBindingManager : public QObject {
  Q_OBJECT

 public:
  struct Binding {
    bool alt = false;
    bool ctrl = false;
    bool shift = false;
    bool command = false;
    bool consume = true;
    QString key;
    QString command_line;
  };

  explicit KeyBindingManager(QObject* parent = nullptr);

  bool AddBinding(Binding binding);
  bool RemoveBinding(const Binding& binding);

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  std::optional<bool> HandleKeyEvent(QKeyEvent* event);
  QString NormalizeKey(const QString& key) const;
  QString ExtractKeyLabel(QKeyEvent* event) const;
  void ExecuteCommand(const QString& command) const;

  std::vector<Binding> bindings_;
};

}  // namespace rethread

#endif  // RETHREAD_BROWSER_KEY_BINDING_MANAGER_H_
