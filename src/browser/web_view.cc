#include "browser/web_view.h"

#include <QContextMenuEvent>
#include <QPalette>
#include <QPoint>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QtGlobal>

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

WebView::WebView(const QString& menu_command,
                 const QColor& background,
                 QWidget* parent)
    : QWebEngineView(parent),
      menu_command_(menu_command),
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
  if (!request || menu_command_.trimmed().isEmpty()) {
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

QString WebView::BuildMenuPayload(
    const QWebEngineContextMenuRequest* request) const {
  QStringList lines;
  lines << QStringLiteral("type_flags=%1").arg(ComputeTypeFlags(request));
  lines << QStringLiteral("x=%1").arg(request->position().x());
  lines << QStringLiteral("y=%1").arg(request->position().y());
  lines << QStringLiteral("editable=%1").arg(request->isContentEditable() ? 1 : 0);
  if (!request->selectedText().isEmpty()) {
    lines << QStringLiteral("selection=%1").arg(
        EncodeField(request->selectedText()));
  }
  if (request->linkUrl().isValid()) {
    lines << QStringLiteral("link_url=%1").arg(
        EncodeField(request->linkUrl().toString()));
  }
  if (request->mediaUrl().isValid()) {
    lines << QStringLiteral("source_url=%1").arg(
        EncodeField(request->mediaUrl().toString()));
  }
  const QUrl frame_url = FrameUrlForRequest(request);
  if (frame_url.isValid()) {
    lines << QStringLiteral("frame_url=%1").arg(
        EncodeField(frame_url.toString()));
  }
  if (!url().isEmpty()) {
    lines << QStringLiteral("page_url=%1").arg(
        EncodeField(url().toString()));
  }
  if (request->mediaType() != QWebEngineContextMenuRequest::MediaTypeNone) {
    lines << QStringLiteral("media_type=%1").arg(
        static_cast<int>(request->mediaType()));
  }
  return lines.join('\n') + '\n';
}

void WebView::RunMenuCommand(const QString& payload) const {
  if (menu_command_.trimmed().isEmpty()) {
    return;
  }
  auto* process = new QProcess(const_cast<WebView*>(this));
  QObject::connect(process,
                   QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                   process, &QObject::deleteLater);
  QObject::connect(process, &QProcess::errorOccurred, process,
                   [process](QProcess::ProcessError) { process->deleteLater(); });
  QObject::connect(process, &QProcess::started, process,
                   [process, payload]() {
                     process->write(payload.toUtf8());
                     process->closeWriteChannel();
                   });
  process->start(QStringLiteral("/bin/sh"),
                 {QStringLiteral("-c"), menu_command_});
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

}  // namespace rethread
