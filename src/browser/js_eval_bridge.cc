#include "browser/js_eval_bridge.h"

namespace rethread {

JsEvalBridge::JsEvalBridge(QObject* parent) : QObject(parent) {}

void JsEvalBridge::Resolve(int request_id, const QVariant& result) {
  emit EvalCompleted(request_id, true, result, QString());
}

void JsEvalBridge::Reject(int request_id, const QString& error_message) {
  emit EvalCompleted(request_id, false, QVariant(), error_message);
}

void JsEvalBridge::NotifyReady() {
  emit Ready();
}

}  // namespace rethread
