#ifndef RETHREAD_BROWSER_WEB_VIEW_H_
#define RETHREAD_BROWSER_WEB_VIEW_H_

#include <QColor>
#include <QWebEngineContextMenuRequest>
#include <QWebEngineLoadingInfo>
#include <QWebEnginePage>
#include <QWebEngineView>

namespace rethread {

class WebView : public QWebEngineView {
  Q_OBJECT

 public:
  explicit WebView(const QString& menu_command,
                   const QColor& background,
                   QWidget* parent = nullptr);

  void BindPageSignals(QWebEnginePage* page);

 protected:
  void contextMenuEvent(QContextMenuEvent* event) override;

 private:
  void HandleLoadingChanged(const QWebEngineLoadingInfo& info);
  QString BuildMenuPayload(const QWebEngineContextMenuRequest* request) const;
  void RunMenuCommand(const QString& payload) const;
  QString EncodeField(const QString& value) const;
  QString ColorToCss(const QColor& color) const;
  int ComputeTypeFlags(const QWebEngineContextMenuRequest* request) const;

  QString menu_command_;
  QColor background_color_;
};

}  // namespace rethread

#endif  // RETHREAD_BROWSER_WEB_VIEW_H_
