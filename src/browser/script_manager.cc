#include "browser/script_manager.h"

#include <algorithm>
#include <cstring>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>

#include "common/debug_log.h"

namespace rethread {
namespace {
constexpr char kScriptSuffix[] = ".user.js";

bool StartsWithUserScriptHeader(const QByteArray& data) {
  if (data.isEmpty()) {
    return false;
  }
  int offset = 0;
  if (data.startsWith("\xEF\xBB\xBF")) {
    offset = 3;
  }
  while (offset < data.size()) {
    const char c = data.at(offset);
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      ++offset;
      continue;
    }
    break;
  }
  return offset < data.size() &&
         data.mid(offset).startsWith("// ==UserScript==");
}

QString JsonQuote(const QString& text) {
  QJsonArray wrapper;
  wrapper.append(text);
  const QJsonDocument doc(wrapper);
  const QByteArray raw = doc.toJson(QJsonDocument::Compact);
  if (raw.size() >= 2 && raw.front() == '[' && raw.back() == ']') {
    return QString::fromUtf8(raw.mid(1, raw.size() - 2));
  }
  return QStringLiteral("\"\"");
}

bool ContainsInvalidIdChars(const QString& id) {
  for (const QChar c : id) {
    if (c.isLetterOrNumber()) {
      continue;
    }
    if (c == QLatin1Char('-') || c == QLatin1Char('_') ||
        c == QLatin1Char('.')) {
      continue;
    }
    return true;
  }
  return false;
}
}  // namespace

ScriptManager::ScriptManager(QWebEngineProfile* profile,
                             const QString& user_data_dir)
    : profile_(profile), user_data_dir_(user_data_dir) {}

ScriptManager::~ScriptManager() = default;

bool ScriptManager::Initialize() {
  QString error;
  if (!EnsureDirectory(&error)) {
    AppendDebugLog("Failed to initialize scripts directory: " +
                   error.toStdString());
    return false;
  }
  return true;
}

bool ScriptManager::AddScript(const QString& id,
                              const QByteArray& source,
                              bool stylesheet,
                              const QString& match_pattern,
                              const QString& run_at_hint,
                              QString* error_message) {
  if (!IsValidScriptId(id)) {
    if (error_message) {
      *error_message = QStringLiteral("invalid --id value");
    }
    return false;
  }

  if (!EnsureDirectory(error_message)) {
    return false;
  }

  QByteArray final_source;
  if (StartsWithUserScriptHeader(source)) {
    final_source = source;
  } else {
    const QString trimmed_match = match_pattern.trimmed();
    if (trimmed_match.isEmpty()) {
      if (error_message) {
        *error_message = QStringLiteral("--match is required for non-UserScript input");
      }
      return false;
    }
    QString run_at =
        CanonicalRunAt(run_at_hint, stylesheet, error_message);
    if (run_at.isEmpty()) {
      return false;
    }
    final_source =
        BuildUserscript(id, source, stylesheet, trimmed_match, run_at);
    if (final_source.isEmpty()) {
      return false;
    }
  }

  const QString path = ScriptPathForId(id);
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    if (error_message) {
      *error_message = QStringLiteral("failed to write %1").arg(path);
    }
    return false;
  }
  if (file.write(final_source) != final_source.size()) {
    if (error_message) {
      *error_message = QStringLiteral("short write while saving %1").arg(path);
    }
    file.close();
    return false;
  }
  file.close();

  QString register_error;
  if (!RegisterScript(id, path, &register_error)) {
    if (error_message) {
      *error_message = register_error.isEmpty()
                           ? QStringLiteral("failed to register script")
                           : register_error;
    }
    return false;
  }

  return true;
}

bool ScriptManager::RemoveScript(const QString& id,
                                 QString* error_message) {
  if (!IsValidScriptId(id)) {
    if (error_message) {
      *error_message = QStringLiteral("invalid --id value");
    }
    return false;
  }

  RemoveFromProfile(id);

  const QString path = ScriptPathForId(id);
  if (QFile::exists(path) && !QFile::remove(path)) {
    if (error_message) {
      *error_message = QStringLiteral("failed to delete %1").arg(path);
    }
    return false;
  }
  script_paths_.remove(id);
  return true;
}

std::vector<ScriptInfo> ScriptManager::ListScripts() const {
  std::vector<ScriptInfo> entries;
  entries.reserve(script_paths_.size());
  for (auto it = script_paths_.cbegin(); it != script_paths_.cend(); ++it) {
    ScriptInfo info;
    info.id = it.key();
    info.path = it.value();
    entries.push_back(info);
  }
  return entries;
}

QString ScriptManager::ScriptsDir() const {
  return user_data_dir_ + QStringLiteral("/scripts");
}

QString ScriptManager::ScriptPathForId(const QString& id) const {
  return ScriptsDir() + QStringLiteral("/") + id + QString::fromLatin1(kScriptSuffix);
}

bool ScriptManager::EnsureDirectory(QString* error_message) const {
  const QString dir_path = ScriptsDir();
  QDir dir(dir_path);
  if (dir.exists()) {
    return true;
  }
  if (!QDir().mkpath(dir_path)) {
    if (error_message) {
      *error_message = QStringLiteral("failed to create %1").arg(dir_path);
    }
    return false;
  }
  return true;
}

bool ScriptManager::RegisterScript(const QString& id,
                                   const QString& path,
                                   QString* error_message) {
  if (!profile_) {
    if (error_message) {
      *error_message = QStringLiteral("profile unavailable");
    }
    return false;
  }
  QWebEngineScriptCollection* collection = profile_->scripts();
  if (!collection) {
    if (error_message) {
      *error_message = QStringLiteral("script collection unavailable");
    }
    return false;
  }

  const QList<QWebEngineScript> existing = collection->find(id);
  for (const auto& script : existing) {
    collection->remove(script);
  }

  QWebEngineScript script;
  script.setSourceUrl(QUrl::fromLocalFile(path));
  script.setName(id);
  script.setWorldId(QWebEngineScript::MainWorld);
  script.setRunsOnSubFrames(true);

  collection->insert(script);
  script_paths_.insert(id, path);
  return true;
}

void ScriptManager::RemoveFromProfile(const QString& id) {
  if (!profile_) {
    return;
  }
  QWebEngineScriptCollection* collection = profile_->scripts();
  if (!collection) {
    return;
  }
  const QList<QWebEngineScript> existing = collection->find(id);
  for (const auto& script : existing) {
    collection->remove(script);
  }
  script_paths_.remove(id);
}

bool ScriptManager::IsValidScriptId(const QString& id) {
  if (id.isEmpty()) {
    return false;
  }
  if (id.contains(QLatin1Char('/')) || id.contains(QLatin1Char('\\'))) {
    return false;
  }
  return !ContainsInvalidIdChars(id);
}

QString ScriptManager::CanonicalRunAt(const QString& run_at_hint,
                                      bool stylesheet,
                                      QString* error_message) const {
  QString value = run_at_hint.trimmed().toLower();
  if (value.isEmpty()) {
    return stylesheet ? QStringLiteral("document-start")
                      : QStringLiteral("document-end");
  }
  if (value == QStringLiteral("document-ready")) {
    value = QStringLiteral("document-end");
  }
  if (value == QStringLiteral("document-start") ||
      value == QStringLiteral("document-end") ||
      value == QStringLiteral("document-idle")) {
    return value;
  }
  if (error_message) {
    *error_message = QStringLiteral("invalid --run-at value");
  }
  return QString();
}

QByteArray ScriptManager::BuildUserscript(const QString& id,
                                          const QByteArray& source,
                                          bool stylesheet,
                                          const QString& match_pattern,
                                          const QString& run_at) const {
  QStringList header_lines;
  header_lines << QStringLiteral("// ==UserScript==");
  header_lines << QStringLiteral("// @name     rethread: %1").arg(id);
  header_lines << QStringLiteral("// @match    %1").arg(match_pattern);
  header_lines << QStringLiteral("// @run-at   %1").arg(run_at);
  header_lines << QStringLiteral("// ==/UserScript==");

  QByteArray result = header_lines.join(QLatin1Char('\n')).toUtf8();
  result.append("\n\n");

  if (stylesheet) {
    QString css_text = QString::fromUtf8(source);
    const QString wrapper = QStringLiteral(
        "(() => {\n"
        "  const css = %1;\n"
        "\n"
        "  function install() {\n"
        "    const root = document.documentElement;\n"
        "    if (!root) {\n"
        "      setTimeout(install, 0);\n"
        "      return;\n"
        "    }\n"
        "    const style = document.createElement(\"style\");\n"
        "    style.textContent = css;\n"
        "    (document.head || root).appendChild(style);\n"
        "  }\n"
        "\n"
        "  install();\n"
        "})();\n")
                               .arg(JsonQuote(css_text));
    result.append(wrapper.toUtf8());
  } else {
    result.append(source);
    if (!source.endsWith('\n')) {
      result.append('\n');
    }
  }

  return result;
}

}  // namespace rethread
