#ifndef RETHREAD_BROWSER_TAB_IPC_SERVER_H_
#define RETHREAD_BROWSER_TAB_IPC_SERVER_H_

#include <QObject>
#include <QPointer>
#include <QString>

class QLocalServer;
class QLocalSocket;

namespace rethread {

class CommandDispatcher;

class TabIpcServer : public QObject {
  Q_OBJECT

 public:
  explicit TabIpcServer(CommandDispatcher* dispatcher, QObject* parent = nullptr);
  ~TabIpcServer() override;

  void Start(const QString& socket_path);
  void Stop();
  QString ExecuteCommand(const QString& command) const;

 private:
  void HandleNewConnection();
  void HandleSocket(QLocalSocket* socket);

  CommandDispatcher* dispatcher_;
  QPointer<QLocalServer> server_;
  QString socket_path_;
};

}  // namespace rethread

#endif  // RETHREAD_BROWSER_TAB_IPC_SERVER_H_
