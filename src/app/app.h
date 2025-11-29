#ifndef RETHREAD_APP_APP_H_
#define RETHREAD_APP_APP_H_

#include "include/cef_app.h"

namespace rethread {

class RethreadApp : public CefApp, public CefBrowserProcessHandler {
 public:
  RethreadApp();

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }

  void OnContextInitialized() override;
  CefRefPtr<CefClient> GetDefaultClient() override;

 private:
  IMPLEMENT_REFCOUNTING(RethreadApp);
};

}  // namespace rethread

#endif  // RETHREAD_APP_APP_H_
