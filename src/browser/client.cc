#include "browser/client.h"

#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_parser.h"
#include "include/internal/cef_types_wrappers.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

#include "browser/key_binding_manager.h"
#include "browser/tab_ipc_server.h"
#include "browser/tab_manager.h"
#include "common/debug_log.h"
#include "common/theme.h"

namespace rethread {
namespace {
BrowserClient* g_browser_client = nullptr;

std::string BuildDataUri(const std::string& data,
                         const std::string& mime_type) {
  return "data:" + mime_type + ";base64," +
         CefURIEncode(CefBase64Encode(data.data(), data.size()), false)
             .ToString();
}

std::string ColorToCssHex(uint32_t color) {
  std::ostringstream oss;
  oss << "#" << std::uppercase << std::setfill('0') << std::hex
      << std::setw(2) << ((color >> 16) & 0xFF) << std::setw(2)
      << ((color >> 8) & 0xFF) << std::setw(2) << (color & 0xFF);
  return oss.str();
}
std::string EncodeField(const std::string& value) {
  return CefURIEncode(value, false).ToString();
}

void AppendField(std::vector<std::string>* lines,
                 const std::string& key,
                 const std::string& value) {
  if (value.empty()) {
    return;
  }
  lines->emplace_back(key + "=" + EncodeField(value));
}

std::string BuildMenuPayload(const CefRefPtr<CefContextMenuParams>& params) {
  std::vector<std::string> lines;
  lines.emplace_back("type_flags=" + std::to_string(params->GetTypeFlags()));
  lines.emplace_back("x=" + std::to_string(params->GetXCoord()));
  lines.emplace_back("y=" + std::to_string(params->GetYCoord()));
  lines.emplace_back("editable=" + std::string(params->IsEditable() ? "1" : "0"));
  AppendField(&lines, "selection",
              params->GetSelectionText().ToString());
  AppendField(&lines, "link_url",
              params->GetLinkUrl().ToString());
  AppendField(&lines, "source_url",
              params->GetSourceUrl().ToString());
  AppendField(&lines, "page_url",
              params->GetPageUrl().ToString());
  AppendField(&lines, "frame_url",
              params->GetFrameUrl().ToString());
  if (params->GetMediaType() != CM_MEDIATYPE_NONE) {
    lines.emplace_back("media_type=" + std::to_string(params->GetMediaType()));
  }
  
  std::ostringstream payload;
  for (const auto& line : lines) {
    payload << line << '\n';
  }
  return payload.str();
}

void LaunchMenuCommand(const std::string& command,
                       const CefRefPtr<CefContextMenuParams>& params) {
  if (command.empty()) {
    return;
  }
  const std::string payload = BuildMenuPayload(params);

  std::ostringstream full_command;
  full_command << "(printf '%s' \"";
  for (char c : payload) {
    if (c == '"' || c == '\\' || c == '$' || c == '`') {
      full_command << '\\';
    }
    if (c == '\n') {
      full_command << "\\n";
      continue;
    }
    full_command << c;
  }
  full_command << "\" | " << command << ") &";

  std::system(full_command.str().c_str());
}

}  // namespace

BrowserClient::BrowserClient(const Options& options) : options_(options) {
  DCHECK(!g_browser_client);
  g_browser_client = this;
}

BrowserClient::~BrowserClient() {
  g_browser_client = nullptr;
}

BrowserClient* BrowserClient::Get() {
  return g_browser_client;
}

void BrowserClient::OnTitleChange(CefRefPtr<CefBrowser> browser,
                                  const CefString& title) {
  CEF_REQUIRE_UI_THREAD();

  if (auto browser_view = CefBrowserView::GetForBrowser(browser)) {
    if (auto window = browser_view->GetWindow()) {
      window->SetTitle(title);
    }
    return;
  }

  if (options_.alloy_runtime) {
    PlatformTitleChange(browser, title);
  }

  if (auto* tab_manager = TabManager::Get()) {
    tab_manager->UpdateBrowserTitle(browser, title.ToString());
  }
}

void BrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  browser_list_.push_back(browser);
}

bool BrowserClient::DoClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  return false;
}

void BrowserClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();

  for (auto it = browser_list_.begin(); it != browser_list_.end(); ++it) {
    if ((*it)->IsSame(browser)) {
      browser_list_.erase(it);
      break;
    }
  }

  AppendDebugLog("Browser closing; remaining=" +
                 std::to_string(browser_list_.size()));
  if (browser_list_.empty()) {
    if (auto* tab_manager = TabManager::Get()) {
      if (auto window = tab_manager->GetWindow()) {
        window->Close();
      }
    }
    TabIpcServer::Get()->Stop();
    AppendDebugLog("No browsers remain; quitting message loop.");
    CefQuitMessageLoop();
  }
}

void BrowserClient::OnLoadError(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                ErrorCode errorCode,
                                const CefString& errorText,
                                const CefString& failedUrl) {
  CEF_REQUIRE_UI_THREAD();

  if (!options_.alloy_runtime) {
    return;
  }

  if (errorCode == ERR_ABORTED) {
    return;
  }

  const uint32_t background_color = GetDefaultBackgroundColor();
  const std::string css_color = ColorToCssHex(background_color);

  std::stringstream ss;
  ss << "<html><body style=\"margin:0;padding:2em;font-family:sans-serif;"
        "background-color:"
     << css_color
     << ";color:#f0f0f0;\">"
        "<h2>Failed to load URL "
     << std::string(failedUrl) << " with error " << std::string(errorText)
     << " (" << errorCode << ").</h2></body></html>";

  frame->LoadURL(BuildDataUri(ss.str(), "text/html"));
}

bool BrowserClient::OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                                  const CefKeyEvent& event,
                                  CefEventHandle os_event,
                                  bool* is_keyboard_shortcut) {
  CEF_REQUIRE_UI_THREAD();
  if (event.type != KEYEVENT_RAWKEYDOWN) {
    return false;
  }
  if ((event.modifiers & EVENTFLAG_IS_REPEAT) != 0) {
    // Avoid launching the handler on every auto-repeat event; this was observed
    // to stall scrolling/typing after long key presses, so we only intercept
    // the initial press and let the page consume repeats directly.
    return false;
  }
  auto result = KeyBindingManager::Get()->HandleKeyEvent(event);
  if (!result.has_value()) {
    return false;
  }
  if (result.value() && is_keyboard_shortcut) {
    *is_keyboard_shortcut = true;
  }
  return result.value();
}

bool BrowserClient::OnKeyEvent(CefRefPtr<CefBrowser> browser,
                               const CefKeyEvent& event,
                               CefEventHandle os_event) {
  return false;
}

void BrowserClient::OnBeforeContextMenu(CefRefPtr<CefBrowser> browser,
                                        CefRefPtr<CefFrame> frame,
                                        CefRefPtr<CefContextMenuParams> params,
                                        CefRefPtr<CefMenuModel> model) {
  CEF_REQUIRE_UI_THREAD();

  if (model) {
    model->Clear();
  }

  LaunchMenuCommand(options_.menu_command, params);
}

void BrowserClient::ShowMainWindow() {
  if (!CefCurrentlyOn(TID_UI)) {
    CefPostTask(TID_UI, base::BindOnce(&BrowserClient::ShowMainWindow, this));
    return;
  }

  if (browser_list_.empty()) {
    return;
  }

  auto primary_browser = browser_list_.front();

  if (auto browser_view = CefBrowserView::GetForBrowser(primary_browser)) {
    if (auto window = browser_view->GetWindow()) {
      window->Show();
      return;
    }
  }

  if (options_.alloy_runtime) {
    PlatformShowWindow(primary_browser);
  }
}

void BrowserClient::CloseAllBrowsers(bool force_close) {
  is_closing_ = true;
  if (!CefCurrentlyOn(TID_UI)) {
    CefPostTask(TID_UI, base::BindOnce(&BrowserClient::CloseAllBrowsers, this,
                                       force_close));
    return;
  }

  if (browser_list_.empty()) {
    return;
  }

  AppendDebugLog(std::string("CloseAllBrowsers called, count=") +
                 std::to_string(browser_list_.size()) +
                 (force_close ? " (force)" : ""));
  BrowserList browsers_to_close = browser_list_;
  for (const auto& browser : browsers_to_close) {
    browser->GetHost()->CloseBrowser(force_close);
  }
}

#if !defined(OS_MAC)
void BrowserClient::PlatformShowWindow(CefRefPtr<CefBrowser> browser) {
  NOTIMPLEMENTED();
}
#endif

}  // namespace rethread
