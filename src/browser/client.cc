#include "browser/client.h"

#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#if defined(OS_POSIX)
#include <sys/wait.h>
#endif

#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_parser.h"
#include "include/internal/cef_types_wrappers.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

#include "browser/tab_ipc_server.h"
#include "browser/tab_manager.h"
#include "common/debug_log.h"
#include "common/theme.h"

namespace rethread {
namespace {
BrowserClient* g_browser_client = nullptr;
constexpr char kKeyHandlerExecutable[] = "rethread-key-handler";

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

bool IsPrintable(int value) {
  return value >= 32 && value <= 126;
}

const char* KeyEventTypeToString(cef_key_event_type_t type) {
  switch (type) {
    case KEYEVENT_RAWKEYDOWN:
      return "rawkeydown";
    case KEYEVENT_KEYDOWN:
      return "keydown";
    case KEYEVENT_KEYUP:
      return "keyup";
    case KEYEVENT_CHAR:
      return "char";
    default:
      return "unknown";
  }
}

void AppendModifierFlag(const CefKeyEvent& event,
                        int flag,
                        const char* name,
                        std::vector<std::string>* args) {
  if ((event.modifiers & flag) != 0) {
    args->push_back(std::string("--") + name);
  }
}

std::string ShellQuote(const std::string& value) {
  std::string quoted = "'";
  for (char c : value) {
    if (c == '\'') {
      quoted += "'\\''";
    } else {
      quoted += c;
    }
  }
  quoted += "'";
  return quoted;
}

bool RunKeyHandler(const CefKeyEvent& event) {
  std::vector<std::string> args;
  args.emplace_back(kKeyHandlerExecutable);
  args.emplace_back("key");
  args.emplace_back(std::string("--type=") + KeyEventTypeToString(event.type));
  args.emplace_back("--windows-key-code=" +
                    std::to_string(event.windows_key_code));
  args.emplace_back("--native-key-code=" +
                    std::to_string(event.native_key_code));
  args.emplace_back("--modifiers=" + std::to_string(event.modifiers));

  auto append_char_arg = [&args](const char* name, int value) {
    if (value <= 0) {
      return;
    }
    args.emplace_back(std::string("--") + name + "=" +
                      std::to_string(value));
  };
  append_char_arg("character", event.character);
  append_char_arg("unmodified-character", event.unmodified_character);

  if (IsPrintable(event.unmodified_character)) {
    std::string label(1, static_cast<char>(event.unmodified_character));
    args.emplace_back(std::string("--key-label=") + label);
  } else if (IsPrintable(event.character)) {
    std::string label(1, static_cast<char>(event.character));
    args.emplace_back(std::string("--key-label=") + label);
  }

  AppendModifierFlag(event, EVENTFLAG_CONTROL_DOWN, "ctrl", &args);
  AppendModifierFlag(event, EVENTFLAG_SHIFT_DOWN, "shift", &args);
  AppendModifierFlag(event, EVENTFLAG_ALT_DOWN, "alt", &args);
  AppendModifierFlag(event, EVENTFLAG_COMMAND_DOWN, "command", &args);
  AppendModifierFlag(event, EVENTFLAG_IS_REPEAT, "repeat", &args);

  std::ostringstream command;
  for (size_t i = 0; i < args.size(); ++i) {
    if (i > 0) {
      command << " ";
    }
    command << ShellQuote(args[i]);
  }

  int status = std::system(command.str().c_str());
  if (status == -1) {
    AppendDebugLog("Failed to execute key handler.");
    return false;
  }
#if defined(OS_POSIX)
  if (WIFEXITED(status)) {
    const int exit_code = WEXITSTATUS(status);
    if (exit_code == 2) {
      return true;
    }
    if (exit_code != 0) {
      AppendDebugLog("Key handler exited with code " +
                     std::to_string(exit_code) + ".");
    }
    return false;
  }
  AppendDebugLog("Key handler terminated abnormally.");
  return false;
#else
  if (status == 2) {
    return true;
  }
  if (status != 0) {
    AppendDebugLog("Key handler exited with code " + std::to_string(status) +
                   ".");
  }
  return false;
#endif
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
  const bool handled = RunKeyHandler(event);
  if (handled && is_keyboard_shortcut) {
    *is_keyboard_shortcut = true;
  }
  return handled;
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
