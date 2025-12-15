#include "browser/rules_manager.h"

#include <QStringList>
#include <string>

#include "common/debug_log.h"

namespace rethread {
namespace {
QString NormalizeLine(const QString& line) {
  QString trimmed = line;
  const int comment_index = trimmed.indexOf(QChar('#'));
  if (comment_index >= 0) {
    trimmed = trimmed.left(comment_index);
  }
  return trimmed.trimmed();
}
}  // namespace

RulesManager::RulesManager(QObject* parent) : QObject(parent) {}

bool RulesManager::LoadJavaScriptRules(ListMode mode,
                                       const QString& raw_text,
                                       int* host_count) {
  javascript_rules_ = BuildRule(mode, raw_text);
  if (host_count) {
    *host_count = static_cast<int>(javascript_rules_.hosts.size());
  }
  AppendDebugLog("Loaded JavaScript rules entries=" +
                 std::to_string(javascript_rules_.hosts.size()) +
                 " mode=" +
                 (mode == ListMode::kAllowlist ? "allowlist" : "blacklist"));
  emit javaScriptRulesChanged();
  return true;
}

bool RulesManager::LoadIframeRules(ListMode mode,
                                   const QString& raw_text,
                                   int* host_count) {
  iframe_rules_ = BuildRule(mode, raw_text);
  if (host_count) {
    *host_count = static_cast<int>(iframe_rules_.hosts.size());
  }
  AppendDebugLog("Loaded iframe rules entries=" +
                 std::to_string(iframe_rules_.hosts.size()) +
                 " mode=" +
                 (mode == ListMode::kAllowlist ? "allowlist" : "blacklist"));
  return true;
}

bool RulesManager::ShouldDisableJavaScript(const QUrl& url) const {
  if (!javascript_rules_.configured || !url.isValid()) {
    return false;
  }
  const QString host = HostFromUrl(url);
  if (host.isEmpty()) {
    return false;
  }
  const bool contains = javascript_rules_.hosts.contains(host);
  if (javascript_rules_.mode == ListMode::kAllowlist) {
    return !contains;
  }
  return contains;
}

bool RulesManager::ShouldBlockIframe(const QUrl& top_level_url,
                                     const QUrl& frame_url) const {
  if (!iframe_rules_.configured || !frame_url.isValid()) {
    return false;
  }
  const QString frame_host = HostFromUrl(frame_url);
  const QString top_host = HostFromUrl(top_level_url);
  const bool frame_match =
      !frame_host.isEmpty() && iframe_rules_.hosts.contains(frame_host);
  const bool top_match =
      !top_host.isEmpty() && iframe_rules_.hosts.contains(top_host);
  if (iframe_rules_.mode == ListMode::kAllowlist) {
    return !(frame_match || top_match);
  }
  return frame_match || top_match;
}

RulesManager::HostRule RulesManager::BuildRule(ListMode mode,
                                               const QString& raw_text) const {
  HostRule rule;
  rule.mode = mode;
  rule.configured = true;
  const QStringList lines = raw_text.split(QChar('\n'));
  for (const QString& original_line : lines) {
    const QString normalized = NormalizeLine(original_line);
    if (normalized.isEmpty()) {
      continue;
    }
    const QString host = NormalizeHost(normalized);
    if (!host.isEmpty()) {
      rule.hosts.insert(host);
    }
  }
  return rule;
}

QString RulesManager::NormalizeHost(const QString& input) const {
  QUrl parsed = QUrl::fromUserInput(input);
  QString host = parsed.isValid() && !parsed.host().isEmpty()
                     ? parsed.host()
                     : input;
  return host.trimmed().toLower();
}

QString RulesManager::HostFromUrl(const QUrl& url) const {
  if (!url.isValid()) {
    return QString();
  }
  return NormalizeHost(url.host());
}

}  // namespace rethread
