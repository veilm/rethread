#ifndef RETHREAD_APP_APP_H_
#define RETHREAD_APP_APP_H_

#include <memory>
#include <optional>
#include <string>

#include <QColor>
#include <QObject>
#include <QString>

class QWebEngineProfile;
class QWebEngineDownloadRequest;

namespace rethread {

class CommandDispatcher;
class ContextMenuBindingManager;
class KeyBindingManager;
class MainWindow;
class RulesManager;
class RulesRequestInterceptor;
class ScriptManager;
class TabIpcServer;
class TabManager;
class TabStripController;

enum class ColorScheme {
  kAuto,
  kLight,
  kDark,
};

struct BrowserOptions {
  QString user_data_dir;
  QString initial_url;
  QString startup_script_path;
  QString debug_log_path;
  QString tab_socket_path;
  QColor background_color = QColor(0x33, 0x33, 0x33);
  int auto_exit_seconds = 0;
  ColorScheme color_scheme = ColorScheme::kDark;
  bool cdp_enabled = true;
  int cdp_port = 9222;
};

class BrowserApplication : public QObject {
  Q_OBJECT

 public:
  BrowserApplication(const BrowserOptions& options, QObject* parent = nullptr);
  ~BrowserApplication() override;

  bool Initialize();
  QString ExecuteCommand(const QString& command) const;

 private:
  void InitializeProfile();
  void InitializeUi();
  void InitializeControllers();
  void InitializeIpc();
  void LoadInitialTab();
  void RunStartupScript() const;
  void ScheduleAutoExit();
  void InitializeDownloadHandling();
  void HandleDownloadRequested(QWebEngineDownloadRequest* request);
  bool RunDownloadHandlerScript(QWebEngineDownloadRequest* request);
  void ApplyDefaultDownloadBehavior(QWebEngineDownloadRequest* request);
  QString DownloadHandlerPath() const;
  void UpdateCdpPortInfo();

  BrowserOptions options_;
  QWebEngineProfile* profile_ = nullptr;
  std::unique_ptr<TabManager> tab_manager_;
  std::unique_ptr<MainWindow> main_window_;
  std::unique_ptr<TabStripController> tab_strip_controller_;
  std::unique_ptr<KeyBindingManager> key_binding_manager_;
  std::unique_ptr<ContextMenuBindingManager> context_menu_binding_manager_;
  std::unique_ptr<RulesManager> rules_manager_;
  std::unique_ptr<RulesRequestInterceptor> rules_interceptor_;
  std::unique_ptr<ScriptManager> script_manager_;
  std::unique_ptr<CommandDispatcher> dispatcher_;
  std::unique_ptr<TabIpcServer> ipc_server_;
};

}  // namespace rethread

#endif  // RETHREAD_APP_APP_H_
