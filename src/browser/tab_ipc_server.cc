#include "browser/tab_ipc_server.h"

#include <memory>

#include <QByteArray>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>

#include "browser/command_dispatcher.h"
#include "common/debug_log.h"

namespace rethread {
TabIpcServer::TabIpcServer(CommandDispatcher* dispatcher, QObject* parent)
    : QObject(parent), dispatcher_(dispatcher) {}

TabIpcServer::~TabIpcServer() {
  Stop();
}

void TabIpcServer::Start(const QString& socket_path) {
  Stop();
  server_ = new QLocalServer(this);
  socket_path_ = socket_path;
  QFile::remove(socket_path_);
  connect(server_, &QLocalServer::newConnection, this,
          &TabIpcServer::HandleNewConnection);
  if (!server_->listen(socket_path_)) {
    AppendDebugLog("Failed to listen on " + socket_path_.toStdString() + ": " +
                   server_->errorString().toStdString());
    server_->deleteLater();
    server_ = nullptr;
  } else {
    AppendDebugLog("Tab IPC server listening on " + socket_path_.toStdString());
  }
}

void TabIpcServer::Stop() {
  if (server_) {
    server_->close();
    server_->deleteLater();
    server_ = nullptr;
  }
  if (!socket_path_.isEmpty()) {
    QFile::remove(socket_path_);
  }
}

QString TabIpcServer::ExecuteCommand(const QString& command) const {
  return dispatcher_ ? dispatcher_->Execute(command) : QString();
}

void TabIpcServer::HandleNewConnection() {
  if (!server_) {
    return;
  }
  while (QLocalSocket* socket = server_->nextPendingConnection()) {
    HandleSocket(socket);
  }
}

void TabIpcServer::HandleSocket(QLocalSocket* socket) {
  if (!socket) {
    return;
  }
  auto buffer = std::make_shared<QByteArray>();
  auto finished = std::make_shared<bool>(false);

  auto process_buffer = [this, socket, buffer, finished]() {
    if (*finished) {
      return;
    }
    *finished = true;
    const QString command =
        QString::fromUtf8(*buffer).trimmed();
    QString response = dispatcher_ ? dispatcher_->Execute(command) : QString();
    if (!response.isEmpty()) {
      socket->write(response.toUtf8());
      socket->flush();
    }
    socket->disconnectFromServer();
    socket->deleteLater();
  };

  connect(socket, &QLocalSocket::readyRead, this,
          [socket, buffer, process_buffer]() {
            buffer->append(socket->readAll());
            if (buffer->contains('\n')) {
              process_buffer();
            }
          });

  connect(socket, &QLocalSocket::disconnected, this,
          [process_buffer]() { process_buffer(); });
}

}  // namespace rethread
