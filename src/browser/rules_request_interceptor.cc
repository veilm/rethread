#include "browser/rules_request_interceptor.h"

#include <QWebEngineUrlRequestInfo>

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
  if (rules_manager_->ShouldBlockIframe(first_party, request_url)) {
    info.block(true);
  }
}

}  // namespace rethread
