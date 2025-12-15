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
  explicit RulesManager(QObject* parent = nullptr);

  bool LoadJavaScriptBlocklist(const QString& raw_text, int* host_count);
  bool ShouldDisableJavaScript(const QUrl& url) const;

 signals:
  void javaScriptRulesChanged();

 private:
  QSet<QString> ParseHosts(const QString& raw_text) const;
  QString ExtractHost(const QString& line) const;

  QSet<QString> javascript_blocked_hosts_;
};

}  // namespace rethread

#endif  // RETHREAD_BROWSER_RULES_MANAGER_H_
