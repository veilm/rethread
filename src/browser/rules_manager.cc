#include "browser/rules_manager.h"

#include <QStringList>
#include <string>

#include "common/debug_log.h"

namespace rethread {

RulesManager::RulesManager(QObject* parent) : QObject(parent) {}

bool RulesManager::LoadJavaScriptBlocklist(const QString& raw_text,
                                           int* host_count) {
  javascript_blocked_hosts_ = ParseHosts(raw_text);
  if (host_count) {
    *host_count = static_cast<int>(javascript_blocked_hosts_.size());
  }
  AppendDebugLog("Loaded JavaScript blocklist entries=" +
                 std::to_string(javascript_blocked_hosts_.size()));
  emit javaScriptRulesChanged();
  return true;
}

bool RulesManager::ShouldDisableJavaScript(const QUrl& url) const {
  if (javascript_blocked_hosts_.empty() || !url.isValid()) {
    return false;
  }
  const QString host = url.host().trimmed().toLower();
  if (host.isEmpty()) {
    return false;
  }
  return javascript_blocked_hosts_.contains(host);
}

QSet<QString> RulesManager::ParseHosts(const QString& raw_text) const {
  QSet<QString> hosts;
  const QStringList lines = raw_text.split(QChar('\n'));
  for (QString line : lines) {
    const QString host = ExtractHost(line);
    if (!host.isEmpty()) {
      hosts.insert(host);
    }
  }
  return hosts;
}

QString RulesManager::ExtractHost(const QString& line) const {
  QString trimmed = line;
  const int comment_index = trimmed.indexOf(QChar('#'));
  if (comment_index >= 0) {
    trimmed = trimmed.left(comment_index);
  }
  trimmed = trimmed.trimmed();
  if (trimmed.isEmpty()) {
    return QString();
  }
  QUrl parsed = QUrl::fromUserInput(trimmed);
  QString host = parsed.isValid() && !parsed.host().isEmpty()
                     ? parsed.host()
                     : trimmed;
  host = host.trimmed().toLower();
  return host;
}

}  // namespace rethread
