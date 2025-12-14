#ifndef RETHREAD_BROWSER_CONTEXT_MENU_BINDING_MANAGER_H_
#define RETHREAD_BROWSER_CONTEXT_MENU_BINDING_MANAGER_H_

#include <QObject>
#include <QString>

namespace rethread {

class ContextMenuBindingManager : public QObject {
  Q_OBJECT

 public:
  explicit ContextMenuBindingManager(QObject* parent = nullptr);

  void SetBinding(const QString& command);
  void ClearBinding();
  bool HasBinding() const;
  QString binding() const;

 private:
  QString binding_command_;
};

}  // namespace rethread

#endif  // RETHREAD_BROWSER_CONTEXT_MENU_BINDING_MANAGER_H_
