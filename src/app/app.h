#ifndef RETHREAD_APP_APP_H_
#define RETHREAD_APP_APP_H_

#include <string>

#include "include/cef_app.h"

namespace rethread {

class RethreadApp : public CefApp, public CefBrowserProcessHandler {
 public:
  struct Options {
    int auto_exit_seconds = 0;
    std::string tab_socket_path;
    std::string startup_script_path;
  };

  explicit RethreadApp(const Options& options);
  ~RethreadApp() override;

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }

  void OnContextInitialized() override;
  CefRefPtr<CefClient> GetDefaultClient() override;

 private:
  const Options options_;

  IMPLEMENT_REFCOUNTING(RethreadApp);
};

}  // namespace rethread

#endif  // RETHREAD_APP_APP_H_
