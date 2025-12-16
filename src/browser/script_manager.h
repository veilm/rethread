#ifndef RETHREAD_BROWSER_SCRIPT_MANAGER_H_
#define RETHREAD_BROWSER_SCRIPT_MANAGER_H_

#include <vector>

#include <QByteArray>
#include <QMap>
#include <QString>

class QWebEngineProfile;

namespace rethread {

struct ScriptInfo {
  QString id;
  QString path;
};

class ScriptManager {
 public:
  ScriptManager(QWebEngineProfile* profile, const QString& user_data_dir);
  ~ScriptManager();

  bool Initialize();

  bool AddScript(const QString& id,
                 const QByteArray& source,
                 bool stylesheet,
                 const QString& match_pattern,
                 const QString& run_at_hint,
                 QString* error_message);

  bool RemoveScript(const QString& id, QString* error_message);

  std::vector<ScriptInfo> ListScripts() const;

 private:
  QString ScriptsDir() const;
  QString ScriptPathForId(const QString& id) const;
  bool EnsureDirectory(QString* error_message) const;
  bool RegisterScript(const QString& id,
                      const QString& path,
                      QString* error_message);
  void RemoveFromProfile(const QString& id);
  static bool IsValidScriptId(const QString& id);

  QString CanonicalRunAt(const QString& run_at_hint,
                         bool stylesheet,
                         QString* error_message) const;
  QByteArray BuildUserscript(const QString& id,
                             const QByteArray& source,
                             bool stylesheet,
                             const QString& match_pattern,
                             const QString& run_at) const;

  QWebEngineProfile* profile_;
  QString user_data_dir_;
  QMap<QString, QString> script_paths_;
};

}  // namespace rethread

#endif  // RETHREAD_BROWSER_SCRIPT_MANAGER_H_
