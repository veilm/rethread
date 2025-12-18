#ifndef RETHREAD_BROWSER_JS_EVAL_BRIDGE_H_
#define RETHREAD_BROWSER_JS_EVAL_BRIDGE_H_

#include <QObject>
#include <QString>
#include <QVariant>

namespace rethread {

// Bridges eval requests/results between C++ and page JS via QWebChannel.
class JsEvalBridge : public QObject {
  Q_OBJECT

 public:
  explicit JsEvalBridge(QObject* parent = nullptr);

 signals:
  // Emitted whenever JS reports completion for a request.
  void EvalCompleted(int request_id,
                     bool success,
                     const QVariant& result,
                     const QString& error);

  // Emitted when the JS helper notifies that the transport is ready.
  void Ready();

 public slots:
  // Called from JS when evaluation succeeds.
  void Resolve(int request_id, const QVariant& result);

  // Called from JS when evaluation throws/rejects.
  void Reject(int request_id, const QString& error_message);

  // Called from JS after the helper script wires up the WebChannel.
  void NotifyReady();
};

}  // namespace rethread

#endif  // RETHREAD_BROWSER_JS_EVAL_BRIDGE_H_
