#include "app/app.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <QUrl>
#include <QWebEngineProfile>
#include <QWebEngineSettings>

#include "browser/command_dispatcher.h"
#include "browser/context_menu_binding_manager.h"
#include "browser/rules_manager.h"
#include "browser/rules_request_interceptor.h"
#include "browser/script_manager.h"
#include "browser/key_binding_manager.h"
#include "browser/main_window.h"
#include "browser/rules_manager.h"
#include "browser/tab_ipc_server.h"
#include "browser/tab_manager.h"
#include "browser/tab_strip_controller.h"
#include "common/debug_log.h"
#include "common/theme.h"

namespace rethread {
namespace {
uint32_t QColorToRgba(const QColor& color) {
  return (static_cast<uint32_t>(color.alpha()) << 24) |
         (static_cast<uint32_t>(color.red()) << 16) |
         (static_cast<uint32_t>(color.green()) << 8) |
         static_cast<uint32_t>(color.blue());
}

QString ProfileStorageName(const QString& path) {
  const QByteArray hash =
      QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1);
  return QStringLiteral("rethread-%1").arg(QString::fromLatin1(hash.toHex()));
}
}  // namespace

BrowserApplication::BrowserApplication(const BrowserOptions& options,
                                       QObject* parent)
    : QObject(parent), options_(options) {
  rules_manager_ = std::make_unique<RulesManager>();
}

BrowserApplication::~BrowserApplication() {
  if (ipc_server_) {
    ipc_server_->Stop();
  }
}

bool BrowserApplication::Initialize() {
  if (!options_.debug_log_path.isEmpty()) {
    SetDebugLogPath(options_.debug_log_path.toStdString());
  }
  SetDefaultBackgroundColor(QColorToRgba(options_.background_color));

  InitializeProfile();
  if (!profile_) {
    return false;
  }

  script_manager_ =
      std::make_unique<ScriptManager>(profile_, options_.user_data_dir);
  if (!script_manager_->Initialize()) {
    return false;
  }

  InitializeUi();
  InitializeControllers();
  InitializeIpc();
  LoadInitialTab();
  RunStartupScript();
  ScheduleAutoExit();
  return true;
}

QString BrowserApplication::ExecuteCommand(const QString& command) const {
  if (!dispatcher_) {
    return QStringLiteral("ERR dispatcher not ready\n");
  }
  return dispatcher_->Execute(command);
}

void BrowserApplication::InitializeProfile() {
  if (profile_) {
    return;
  }
  QDir().mkpath(options_.user_data_dir);
  QDir().mkpath(options_.user_data_dir + QStringLiteral("/cache"));
  profile_ = new QWebEngineProfile(ProfileStorageName(options_.user_data_dir),
                                   this);
  profile_->setPersistentStoragePath(options_.user_data_dir);
  profile_->setCachePath(options_.user_data_dir + QStringLiteral("/cache"));
  profile_->setPersistentCookiesPolicy(
      QWebEngineProfile::AllowPersistentCookies);
  profile_->setSpellCheckEnabled(false);
  if (profile_->settings()) {
    profile_->settings()->setAttribute(
        QWebEngineSettings::ScrollAnimatorEnabled, true);
  }
  rules_interceptor_ =
      std::make_unique<RulesRequestInterceptor>(rules_manager_.get());
  profile_->setUrlRequestInterceptor(rules_interceptor_.get());
}

void BrowserApplication::InitializeUi() {
  tab_manager_ =
      std::make_unique<TabManager>(profile_, options_.background_color);
  main_window_ = std::make_unique<MainWindow>(*tab_manager_);
  tab_manager_->setContainer(main_window_->tabStack());
}

void BrowserApplication::InitializeControllers() {
  tab_strip_controller_ =
      std::make_unique<TabStripController>(main_window_->tabStripOverlay());
  QObject::connect(
      tab_manager_.get(), &TabManager::tabsChanged,
      tab_strip_controller_.get(), &TabStripController::SetTabs);
  QObject::connect(tab_manager_.get(), &TabManager::allTabsClosed, this, []() {
    QCoreApplication::quit();
  });

  key_binding_manager_ = std::make_unique<KeyBindingManager>();
  context_menu_binding_manager_ =
      std::make_unique<ContextMenuBindingManager>();
  if (tab_manager_) {
    tab_manager_->setContextMenuBindingManager(
        context_menu_binding_manager_.get());
    tab_manager_->setRulesManager(rules_manager_.get());
  }
}

void BrowserApplication::InitializeIpc() {
  dispatcher_ = std::make_unique<CommandDispatcher>(
      tab_manager_.get(), key_binding_manager_.get(),
      context_menu_binding_manager_.get(), rules_manager_.get(),
      script_manager_.get(), tab_strip_controller_.get());

  if (options_.tab_socket_path.isEmpty()) {
    return;
  }
  ipc_server_ = std::make_unique<TabIpcServer>(dispatcher_.get());
  ipc_server_->Start(options_.tab_socket_path);
}

void BrowserApplication::LoadInitialTab() {
  if (!tab_manager_) {
    return;
  }
  const QUrl url = QUrl::fromUserInput(options_.initial_url);
  tab_manager_->openTab(url, true);
  main_window_->show();
}

void BrowserApplication::RunStartupScript() const {
  if (options_.startup_script_path.isEmpty()) {
    return;
  }
  QFileInfo info(options_.startup_script_path);
  if (!info.exists() || !info.isFile()) {
    AppendDebugLog("Startup script missing: " +
                   options_.startup_script_path.toStdString());
    return;
  }

  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  if (!options_.user_data_dir.isEmpty()) {
    env.insert(QStringLiteral("RETHREAD_USER_DATA_DIR"),
               options_.user_data_dir);
  }
  if (!options_.tab_socket_path.isEmpty()) {
    env.insert(QStringLiteral("RETHREAD_TAB_SOCKET"),
               options_.tab_socket_path);
  }

  auto* process = new QProcess(const_cast<BrowserApplication*>(this));
  process->setWorkingDirectory(options_.user_data_dir);
  process->setProcessEnvironment(env);
  QObject::connect(
      process,
      QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
      process,
      [process](int exit_code, QProcess::ExitStatus status) {
        if (status != QProcess::NormalExit || exit_code != 0) {
          AppendDebugLog("Startup script exited with code " +
                         std::to_string(exit_code));
        }
        process->deleteLater();
      });
  QObject::connect(process, &QProcess::errorOccurred, process,
                   [process](QProcess::ProcessError error) {
                     AppendDebugLog("Startup script process error " +
                                    std::to_string(static_cast<int>(error)));
                   });
  process->start(QStringLiteral("/bin/sh"),
                 {options_.startup_script_path});
  if (!process->waitForStarted()) {
    AppendDebugLog("Failed to start startup script: " +
                   options_.startup_script_path.toStdString());
    process->deleteLater();
  }
}

void BrowserApplication::ScheduleAutoExit() {
  if (options_.auto_exit_seconds <= 0) {
    return;
  }
  QTimer::singleShot(options_.auto_exit_seconds * 1000, this, []() {
    QCoreApplication::quit();
  });
}

}  // namespace rethread
