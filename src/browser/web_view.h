#ifndef RETHREAD_BROWSER_WEB_VIEW_H_
#define RETHREAD_BROWSER_WEB_VIEW_H_

#include <QColor>
#include <QList>
#include <QWebEngineContextMenuRequest>
#include <QWebEngineLoadingInfo>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <utility>

class QProcess;

namespace rethread {

class ContextMenuBindingManager;

class WebView : public QWebEngineView {
  Q_OBJECT

 public:
  explicit WebView(ContextMenuBindingManager* context_menu_binding_manager,
                   const QColor& background,
                   QWidget* parent = nullptr);

  void BindPageSignals(QWebEnginePage* page);

 protected:
  void contextMenuEvent(QContextMenuEvent* event) override;

 private:
  void HandleLoadingChanged(const QWebEngineLoadingInfo& info);
  struct MenuPayload {
    QString raw_payload;
    QList<std::pair<QString, QString>> fields;
  };
  MenuPayload BuildMenuPayload(
      const QWebEngineContextMenuRequest* request) const;
  void RunMenuCommand(const MenuPayload& payload) const;
  QString EncodeField(const QString& value) const;
  QString ColorToCss(const QColor& color) const;
  int ComputeTypeFlags(const QWebEngineContextMenuRequest* request) const;
  void PopulatePayloadFields(MenuPayload* payload,
                             const QString& key,
                             const QString& value) const;
  void InjectMenuEnvironment(QProcess* process,
                             const MenuPayload& payload) const;

  ContextMenuBindingManager* context_menu_binding_manager_ = nullptr;
  QColor background_color_;
};

}  // namespace rethread

#endif  // RETHREAD_BROWSER_WEB_VIEW_H_
