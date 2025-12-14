#include "browser/web_view.h"

#include <QContextMenuEvent>
#include <QPalette>
#include <QPoint>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QtGlobal>

#include "browser/context_menu_binding_manager.h"

namespace rethread {
namespace {
constexpr int kTypeFlagPage = 1 << 0;
constexpr int kTypeFlagFrame = 1 << 1;
constexpr int kTypeFlagLink = 1 << 2;
constexpr int kTypeFlagMedia = 1 << 3;
constexpr int kTypeFlagSelection = 1 << 4;
constexpr int kTypeFlagEditable = 1 << 5;
QUrl FrameUrlForRequest(const QWebEngineContextMenuRequest* request) {
  if (!request) {
    return QUrl();
  }
  QVariant value = request->property("frameUrl");
  if (value.canConvert<QUrl>()) {
    return value.toUrl();
  }
  return QUrl();
}

}  // namespace

WebView::WebView(ContextMenuBindingManager* context_menu_binding_manager,
                 const QColor& background,
                 QWidget* parent)
    : QWebEngineView(parent),
      context_menu_binding_manager_(context_menu_binding_manager),
      background_color_(background) {
  QPalette pal = palette();
  pal.setColor(QPalette::Base, background_color_);
  pal.setColor(QPalette::Window, background_color_);
  setPalette(pal);
  setAutoFillBackground(true);
}

void WebView::BindPageSignals(QWebEnginePage* page) {
  if (!page) {
    return;
  }
  connect(page, &QWebEnginePage::loadingChanged, this,
          &WebView::HandleLoadingChanged);
}

void WebView::contextMenuEvent(QContextMenuEvent* event) {
  event->accept();
  const auto* request = lastContextMenuRequest();
  if (!request) {
    return;
  }
  RunMenuCommand(BuildMenuPayload(request));
}

void WebView::HandleLoadingChanged(const QWebEngineLoadingInfo& info) {
  if (info.status() != QWebEngineLoadingInfo::LoadFailedStatus) {
    return;
  }
  const QString html =
      QStringLiteral("<html><body style=\"margin:0;padding:2em;font-family:"
                     "sans-serif;background-color:%1;color:#f0f0f0;\">"
                     "<h2>Failed to load URL %2</h2>"
                     "<p>Error: %3 (%4)</p></body></html>")
          .arg(ColorToCss(background_color_),
               info.url().toString().toHtmlEscaped(),
               info.errorString().toHtmlEscaped())
          .arg(info.errorCode());
  setHtml(html, info.url());
}

WebView::MenuPayload WebView::BuildMenuPayload(
    const QWebEngineContextMenuRequest* request) const {
  MenuPayload payload;
  PopulatePayloadFields(&payload, QStringLiteral("type_flags"),
                        QString::number(ComputeTypeFlags(request)));
  PopulatePayloadFields(&payload, QStringLiteral("x"),
                        QString::number(request->position().x()));
  PopulatePayloadFields(&payload, QStringLiteral("y"),
                        QString::number(request->position().y()));
  PopulatePayloadFields(
      &payload, QStringLiteral("editable"),
      QString::number(request->isContentEditable() ? 1 : 0));
  if (!request->selectedText().isEmpty()) {
    PopulatePayloadFields(&payload, QStringLiteral("selection"),
                          EncodeField(request->selectedText()));
  }
  if (request->linkUrl().isValid()) {
    PopulatePayloadFields(&payload, QStringLiteral("link_url"),
                          EncodeField(request->linkUrl().toString()));
  }
  if (request->mediaUrl().isValid()) {
    PopulatePayloadFields(&payload, QStringLiteral("source_url"),
                          EncodeField(request->mediaUrl().toString()));
  }
  const QUrl frame_url = FrameUrlForRequest(request);
  if (frame_url.isValid()) {
    PopulatePayloadFields(&payload, QStringLiteral("frame_url"),
                          EncodeField(frame_url.toString()));
  }
  if (!url().isEmpty()) {
    PopulatePayloadFields(&payload, QStringLiteral("page_url"),
                          EncodeField(url().toString()));
  }
  if (request->mediaType() != QWebEngineContextMenuRequest::MediaTypeNone) {
    PopulatePayloadFields(&payload, QStringLiteral("media_type"),
                          QString::number(static_cast<int>(request->mediaType())));
  }
  return payload;
}

void WebView::RunMenuCommand(const MenuPayload& payload) const {
  if (!context_menu_binding_manager_ ||
      !context_menu_binding_manager_->HasBinding()) {
    return;
  }
  const QString command = context_menu_binding_manager_->binding().trimmed();
  if (command.isEmpty()) {
    return;
  }
  auto* process = new QProcess(const_cast<WebView*>(this));
  QObject::connect(process,
                   QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                   process, &QObject::deleteLater);
  QObject::connect(process, &QProcess::errorOccurred, process,
                   [process](QProcess::ProcessError) { process->deleteLater(); });
  InjectMenuEnvironment(process, payload);
  process->start(QStringLiteral("/bin/sh"),
                 {QStringLiteral("-c"), command});
}

QString WebView::EncodeField(const QString& value) const {
  return QString::fromUtf8(QUrl::toPercentEncoding(value));
}

QString WebView::ColorToCss(const QColor& color) const {
  return QStringLiteral("#%1%2%3")
      .arg(color.red(), 2, 16, QLatin1Char('0'))
      .arg(color.green(), 2, 16, QLatin1Char('0'))
      .arg(color.blue(), 2, 16, QLatin1Char('0'))
      .toUpper();
}

int WebView::ComputeTypeFlags(
    const QWebEngineContextMenuRequest* request) const {
  int flags = kTypeFlagPage;
  if (request->linkUrl().isValid()) {
    flags |= kTypeFlagLink;
  }
  if (request->mediaType() != QWebEngineContextMenuRequest::MediaTypeNone) {
    flags |= kTypeFlagMedia;
  }
  if (!request->selectedText().isEmpty()) {
    flags |= kTypeFlagSelection;
  }
  if (request->isContentEditable()) {
    flags |= kTypeFlagEditable;
  }
  return flags;
}

void WebView::PopulatePayloadFields(MenuPayload* payload,
                                    const QString& key,
                                    const QString& value) const {
  if (!payload || value.isEmpty()) {
    return;
  }
  payload->fields.push_back(std::make_pair(key, value));
  payload->raw_payload.append(
      QStringLiteral("%1=%2\n").arg(key, value));
}

void WebView::InjectMenuEnvironment(QProcess* process,
                                    const MenuPayload& payload) const {
  if (!process) {
    return;
  }
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  if (!payload.raw_payload.isEmpty()) {
    env.insert(QStringLiteral("RETHREAD_CONTEXT_PAYLOAD"),
               payload.raw_payload);
  }
  for (const auto& field : payload.fields) {
    QString normalized = field.first.toUpper();
    normalized.replace(QChar('-'), QChar('_'));
    env.insert(QStringLiteral("RETHREAD_CONTEXT_%1").arg(normalized),
               field.second);
  }
  process->setProcessEnvironment(env);
}

}  // namespace rethread
