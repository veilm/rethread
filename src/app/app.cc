#include "app/app.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QTimer>
#include <QWebEngineProfile>
#include <QUrl>

#include "browser/command_dispatcher.h"
#include "browser/key_binding_manager.h"
#include "browser/main_window.h"
#include "browser/tab_ipc_server.h"
#include "browser/tab_manager.h"
#include "browser/tab_strip_controller.h"
#include "common/debug_log.h"
#include "common/theme.h"

namespace rethread {
namespace {
QString TrimmedLine(const QString& input) {
  QString trimmed = input.trimmed();
  if (trimmed.startsWith('#')) {
    return QString();
  }
  return trimmed;
}

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
    : QObject(parent), options_(options) {}

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
}

void BrowserApplication::InitializeUi() {
  tab_manager_ =
      std::make_unique<TabManager>(profile_, options_.background_color);
  tab_manager_->setMenuCommand(options_.menu_command);
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
}

void BrowserApplication::InitializeIpc() {
  dispatcher_ = std::make_unique<CommandDispatcher>(
      tab_manager_.get(), key_binding_manager_.get(),
      tab_strip_controller_.get());

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
  if (!dispatcher_ || options_.startup_script_path.isEmpty()) {
    return;
  }
  QFile script(options_.startup_script_path);
  if (!script.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return;
  }
  QTextStream stream(&script);
  while (!stream.atEnd()) {
    const QString raw_line = stream.readLine();
    const QString line = TrimmedLine(raw_line);
    if (line.isEmpty()) {
      continue;
    }
    const QString response = dispatcher_->Execute(line);
    if (!response.trimmed().isEmpty()) {
      AppendDebugLog("startup cmd: " + line.toStdString() + " -> " +
                     response.trimmed().toStdString());
    }
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
