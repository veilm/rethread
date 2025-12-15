#ifndef RETHREAD_BROWSER_RULES_REQUEST_INTERCEPTOR_H_
#define RETHREAD_BROWSER_RULES_REQUEST_INTERCEPTOR_H_

#include <QWebEngineUrlRequestInterceptor>

namespace rethread {

class RulesManager;

class RulesRequestInterceptor : public QWebEngineUrlRequestInterceptor {
 public:
  explicit RulesRequestInterceptor(RulesManager* rules_manager);

  void interceptRequest(QWebEngineUrlRequestInfo& info) override;

 private:
  RulesManager* rules_manager_ = nullptr;
};

}  // namespace rethread

#endif  // RETHREAD_BROWSER_RULES_REQUEST_INTERCEPTOR_H_
