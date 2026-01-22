#include "browser/rules_request_interceptor.h"

#include <QWebEngineUrlRequestInfo>

#include "common/debug_log.h"
#include "browser/rules_manager.h"

namespace rethread {

RulesRequestInterceptor::RulesRequestInterceptor(RulesManager* rules_manager)
    : rules_manager_(rules_manager) {}

void RulesRequestInterceptor::interceptRequest(
    QWebEngineUrlRequestInfo& info) {
  if (!rules_manager_ ||
      info.resourceType() != QWebEngineUrlRequestInfo::ResourceTypeSubFrame) {
    return;
  }
  const QUrl first_party = info.firstPartyUrl();
  const QUrl request_url = info.requestUrl();
  QString reason;
  if (rules_manager_->ShouldBlockIframe(first_party, request_url, &reason)) {
    const QString top_host = first_party.host();
    const QString frame_host = request_url.host();
    AppendDebugLog("Blocked iframe top=" + top_host.toStdString() +
                   " frame=" + frame_host.toStdString() +
                   " reason=" + reason.toStdString());
    info.block(true);
  }
}

}  // namespace rethread
