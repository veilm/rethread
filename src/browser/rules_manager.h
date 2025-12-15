#ifndef RETHREAD_BROWSER_RULES_MANAGER_H_
#define RETHREAD_BROWSER_RULES_MANAGER_H_

#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>

namespace rethread {

class RulesManager : public QObject {
  Q_OBJECT

 public:
  enum class ListMode {
    kAllowlist,
    kBlacklist,
  };

  explicit RulesManager(QObject* parent = nullptr);

  bool LoadJavaScriptRules(ListMode mode,
                           const QString& raw_text,
                           bool append,
                           int* host_count);
  bool LoadIframeRules(ListMode mode,
                       const QString& raw_text,
                       bool append,
                       int* host_count);

  bool ShouldDisableJavaScript(const QUrl& url) const;
  bool ShouldBlockIframe(const QUrl& top_level_url,
                         const QUrl& frame_url) const;

 signals:
  void javaScriptRulesChanged();

 private:
  struct HostRule {
    ListMode mode = ListMode::kBlacklist;
    bool configured = false;
    QSet<QString> hosts;
  };

  HostRule BuildRule(ListMode mode, const QString& raw_text) const;
  bool ApplyRuleUpdate(HostRule* target,
                       ListMode mode,
                       const QString& raw_text,
                       bool append);
  QString NormalizeHost(const QString& input) const;
  QString HostFromUrl(const QUrl& url) const;

  HostRule javascript_rules_;
  HostRule iframe_rules_;
};

}  // namespace rethread

#endif  // RETHREAD_BROWSER_RULES_MANAGER_H_
