#include "app/app.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QWebEngineDownloadRequest>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>

#include "app/user_dirs.h"
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
  UpdateCdpPortInfo();

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
  InitializeDownloadHandling();
}

void BrowserApplication::InitializeDownloadHandling() {
  if (!profile_) {
    return;
  }
  connect(profile_, &QWebEngineProfile::downloadRequested, this,
          &BrowserApplication::HandleDownloadRequested);
}

void BrowserApplication::HandleDownloadRequested(
    QWebEngineDownloadRequest* request) {
  if (!request ||
      request->state() != QWebEngineDownloadRequest::DownloadRequested) {
    return;
  }
  if (RunDownloadHandlerScript(request)) {
    return;
  }
  ApplyDefaultDownloadBehavior(request);
}

bool BrowserApplication::RunDownloadHandlerScript(
    QWebEngineDownloadRequest* request) {
  const QString handler_path = DownloadHandlerPath();
  QFileInfo handler_info(handler_path);
  if (!handler_info.exists() || !handler_info.isFile()) {
    return false;
  }

  QJsonObject payload;
  payload.insert(QStringLiteral("url"), request->url().toString());
  payload.insert(QStringLiteral("mime_type"), request->mimeType());
  payload.insert(QStringLiteral("suggested_file_name"),
                 request->suggestedFileName());
  payload.insert(QStringLiteral("download_directory"),
                 request->downloadDirectory());
  payload.insert(QStringLiteral("download_file_name"),
                 request->downloadFileName());
  payload.insert(QStringLiteral("state"),
                 static_cast<int>(request->state()));
  payload.insert(QStringLiteral("interrupt_reason"),
                 static_cast<int>(request->interruptReason()));
  payload.insert(QStringLiteral("save_page_format"),
                 static_cast<int>(request->savePageFormat()));
  payload.insert(QStringLiteral("total_bytes"),
                 static_cast<qint64>(request->totalBytes()));
  payload.insert(QStringLiteral("received_bytes"),
                 static_cast<qint64>(request->receivedBytes()));
  if (request->page()) {
    payload.insert(QStringLiteral("page_url"),
                   request->page()->url().toString());
  }

  QProcess process;
  process.setProgram(handler_path);
  process.setProcessChannelMode(QProcess::SeparateChannels);

  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  if (!options_.user_data_dir.isEmpty()) {
    env.insert(QStringLiteral("RETHREAD_USER_DATA_DIR"),
               options_.user_data_dir);
  }
  if (!options_.tab_socket_path.isEmpty()) {
    env.insert(QStringLiteral("RETHREAD_TAB_SOCKET"),
               options_.tab_socket_path);
  }
  process.setProcessEnvironment(env);

  QByteArray stdin_payload =
      QJsonDocument(payload).toJson(QJsonDocument::Compact);
  stdin_payload.append('\n');
  process.start();
  if (!process.waitForStarted(1000)) {
    AppendDebugLog("Failed to start download handler: " +
                   handler_path.toStdString());
    return false;
  }
  process.write(stdin_payload);
  process.closeWriteChannel();
  if (!process.waitForFinished(10000)) {
    AppendDebugLog("Download handler timed out");
    process.kill();
    process.waitForFinished();
    return false;
  }

  const QByteArray stdout_data = process.readAllStandardOutput().trimmed();
  if (stdout_data.isEmpty()) {
    AppendDebugLog("Download handler produced no output");
    return false;
  }

  QJsonParseError parse_error;
  const QJsonDocument response =
      QJsonDocument::fromJson(stdout_data, &parse_error);
  if (parse_error.error != QJsonParseError::NoError) {
    AppendDebugLog("Download handler response parse error: " +
                   parse_error.errorString().toStdString());
    return false;
  }
  if (!response.isObject()) {
    AppendDebugLog("Download handler response must be an object");
    return false;
  }

  const QJsonObject decision = response.object();
  const bool accept =
      decision.value(QStringLiteral("accept")).toBool(true);
  if (!accept) {
    request->cancel();
    return true;
  }

  const QString path_value =
      decision.value(QStringLiteral("path")).toString();
  if (!path_value.isEmpty()) {
    QFileInfo info(path_value);
    if (!info.absolutePath().isEmpty()) {
      request->setDownloadDirectory(info.absolutePath());
    }
    if (!info.fileName().isEmpty()) {
      request->setDownloadFileName(info.fileName());
    }
  } else {
    const QString directory =
        decision.value(QStringLiteral("directory")).toString();
    const QString filename =
        decision.value(QStringLiteral("filename")).toString();
    if (!directory.isEmpty()) {
      request->setDownloadDirectory(directory);
    }
    if (!filename.isEmpty()) {
      request->setDownloadFileName(filename);
    }
  }

  if (request->downloadFileName().isEmpty()) {
    QString fallback = request->suggestedFileName();
    if (fallback.isEmpty()) {
      fallback = QStringLiteral("download");
    }
    request->setDownloadFileName(fallback);
  }

  ApplyDefaultDownloadBehavior(request);
  return true;
}

void BrowserApplication::ApplyDefaultDownloadBehavior(
    QWebEngineDownloadRequest* request) {
  if (!request) {
    return;
  }
  QString directory = request->downloadDirectory();
  if (directory.isEmpty()) {
    directory =
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (directory.isEmpty()) {
      directory = options_.user_data_dir;
    }
    if (!directory.isEmpty()) {
      request->setDownloadDirectory(directory);
    }
  }
  QString file_name = request->downloadFileName();
  if (file_name.isEmpty()) {
    file_name = request->suggestedFileName();
    if (file_name.isEmpty()) {
      file_name = QStringLiteral("download");
    }
    request->setDownloadFileName(file_name);
  }
  request->accept();
}

QString BrowserApplication::DownloadHandlerPath() const {
  return QString::fromStdString(DefaultConfigDir()) +
         QStringLiteral("/rethread-download-handler");
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
  process->setProcessChannelMode(QProcess::ForwardedChannels);
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

void BrowserApplication::UpdateCdpPortInfo() {
  const std::string path =
      CdpPortPath(options_.user_data_dir.toStdString());
  if (path.empty()) {
    return;
  }
  const QString qpath = QString::fromStdString(path);
  if (!options_.cdp_enabled || options_.cdp_port <= 0 ||
      options_.cdp_port >= 65536) {
    QFile::remove(qpath);
    return;
  }
  QFile file(qpath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    AppendDebugLog("Failed to write CDP port file: " +
                   qpath.toStdString());
    return;
  }
  const QByteArray data =
      QByteArray::number(options_.cdp_port) + QByteArrayLiteral("\n");
  file.write(data);
}

}  // namespace rethread
