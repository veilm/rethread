#include "app/app.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

#include "include/cef_app.h"

#if defined(CEF_X11)
#include <X11/Xlib.h>
#endif

#if defined(__linux__)
#include <glib.h>
#endif

#include "app/tab_cli.h"
#include "browser/tab_ipc_server.h"
#include "common/debug_log.h"
#include "common/theme.h"
#include "include/base/cef_logging.h"
#include "include/cef_command_line.h"

namespace {

struct CliOptions {
  bool show_help = false;
  std::string user_data_dir;
  uint32_t background_color = rethread::kDefaultBackgroundColor;
  std::string debug_log_path;
  int auto_exit_seconds = 0;
};

std::string GetEnv(const char* key) {
  const char* value = std::getenv(key);
  if (!value) {
    return std::string();
  }
  return std::string(value);
}

std::string DefaultUserDataDir() {
  std::string base = GetEnv("XDG_DATA_HOME");
  if (base.empty()) {
    std::string home = GetEnv("HOME");
    if (home.empty()) {
      return "rethread";
    }
    base = home + "/.local/share";
  }
  return base + "/rethread";
}

bool ParseColorValue(const std::string& input, uint32_t* color) {
  if (!color || input.empty()) {
    return false;
  }

  std::string trimmed = input;
  if (trimmed.rfind("0x", 0) == 0 || trimmed.rfind("0X", 0) == 0) {
    trimmed = trimmed.substr(2);
  }
  if (!trimmed.empty() && trimmed[0] == '#') {
    trimmed = trimmed.substr(1);
  }

  if (trimmed.size() != 6 && trimmed.size() != 8) {
    return false;
  }

  for (char c : trimmed) {
    if (!std::isxdigit(static_cast<unsigned char>(c))) {
      return false;
    }
  }

  uint32_t parsed = 0;
  try {
    parsed = static_cast<uint32_t>(std::stoul(trimmed, nullptr, 16));
  } catch (...) {
    return false;
  }

  if (trimmed.size() == 6) {
    parsed |= 0xFF000000;
  }

  *color = parsed;
  return true;
}

CliOptions ParseCliOptions(int argc, char* argv[]) {
  CliOptions options;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      options.show_help = true;
      continue;
    }

    const std::string user_data_prefix = "--user-data-dir=";
    if (arg.rfind(user_data_prefix, 0) == 0) {
      options.user_data_dir = arg.substr(user_data_prefix.size());
      continue;
    }
    if (arg == "--user-data-dir" && i + 1 < argc) {
      options.user_data_dir = argv[++i];
      continue;
    }

    const std::string color_prefix = "--background-color=";
    if (arg.rfind(color_prefix, 0) == 0) {
      uint32_t parsed_color = 0;
      if (ParseColorValue(arg.substr(color_prefix.size()), &parsed_color)) {
        options.background_color = parsed_color;
      } else {
        std::cerr << "Ignoring invalid --background-color value: "
                  << arg.substr(color_prefix.size()) << std::endl;
      }
      continue;
    }
    if (arg == "--background-color" && i + 1 < argc) {
      uint32_t parsed_color = 0;
      if (ParseColorValue(argv[i + 1], &parsed_color)) {
        options.background_color = parsed_color;
      } else {
        std::cerr << "Ignoring invalid --background-color value: "
                  << argv[i + 1] << std::endl;
      }
      ++i;
      continue;
    }

    const std::string debug_log_prefix = "--debug-log=";
    if (arg.rfind(debug_log_prefix, 0) == 0) {
      options.debug_log_path = arg.substr(debug_log_prefix.size());
      continue;
    }
    if (arg == "--debug-log" && i + 1 < argc) {
      options.debug_log_path = argv[++i];
      continue;
    }

    const std::string auto_exit_prefix = "--auto-exit=";
    if (arg.rfind(auto_exit_prefix, 0) == 0) {
      options.auto_exit_seconds = std::atoi(arg.substr(auto_exit_prefix.size()).c_str());
      continue;
    }
    if (arg == "--auto-exit" && i + 1 < argc) {
      options.auto_exit_seconds = std::atoi(argv[++i]);
      continue;
    }
  }

  if (options.user_data_dir.empty()) {
    options.user_data_dir = DefaultUserDataDir();
  }

  return options;
}

void PrintHelp() {
  std::cout
      << "Usage:\n"
      << "  rethread [options]\n"
      << "  rethread tabs [--user-data-dir=PATH] <command>\n"
      << "\n"
      << "Browser options:\n"
      << "  --help, -h              Show this help and exit.\n"
      << "  --user-data-dir=PATH    Override the profile directory (defaults to\n"
      << "                          $XDG_DATA_HOME/rethread or\n"
      << "                          $HOME/.local/share/rethread).\n"
      << "  --background-color=HEX  Default background color in #RRGGBB or\n"
      << "                          #AARRGGBB format.\n"
      << "  --url=URL               Initial page to load (defaults to\n"
      << "                          https://veilm.github.io/rethread/).\n"
      << "  --debug-log=PATH        Append debug output to PATH (e.g.\n"
      << "                          /tmp/rethread-debug.log).\n"
      << "  --auto-exit=SECONDS     Quit automatically after SECONDS (best effort,\n"
      << "                          useful for automation).\n"
      << "\n"
      << "Tab/IPC commands:\n"
      << "  rethread tabs list      Print the currently open tabs (alias: get).\n"
      << "  rethread tabs switch <id>\n"
      << "                          Focus the tab with numeric id <id>.\n"
      << "  rethread tabs open <url>\n"
      << "                          Open <url> in a new tab (remaining args are\n"
      << "                          joined with spaces).\n"
      << "  --user-data-dir works the same here to pick which profile/socket to\n"
      << "  talk to if you have multiple instances.\n";
}

#if defined(CEF_X11)
int XErrorHandlerImpl(Display* display, XErrorEvent* event) {
  LOG(WARNING) << "X error received: " << "type " << event->type << ", "
               << "serial " << event->serial << ", " << "error_code "
               << static_cast<int>(event->error_code) << ", " << "request_code "
               << static_cast<int>(event->request_code) << ", " << "minor_code "
               << static_cast<int>(event->minor_code);
  return 0;
}

int XIOErrorHandlerImpl(Display* display) {
  return 0;
}
#endif  // defined(CEF_X11)

#if defined(__linux__)
void IBusLogHandler(const gchar* log_domain,
                    GLogLevelFlags log_level,
                    const gchar* message,
                    gpointer user_data) {
  if (message && std::strstr(message, "Unable to connect to ibus")) {
    return;
  }

  g_log_default_handler(log_domain, log_level, message, user_data);
}

void SuppressIbusWarnings() {
  g_log_set_handler("IBus",
                    static_cast<GLogLevelFlags>(G_LOG_LEVEL_WARNING |
                                                G_LOG_LEVEL_CRITICAL |
                                                G_LOG_LEVEL_ERROR),
                    IBusLogHandler, nullptr);
}
#endif  // defined(__linux__)

}  // namespace

using rethread::RethreadApp;
using rethread::TabIpcServer;

NO_STACK_PROTECTOR
int main(int argc, char* argv[]) {
  if (argc >= 2 && std::string(argv[1]) == "tabs") {
    return rethread::RunTabCli(argc - 2, argv + 2, DefaultUserDataDir());
  }

  CliOptions cli_options = ParseCliOptions(argc, argv);
  if (cli_options.show_help) {
    PrintHelp();
    return 0;
  }

  std::error_code dir_err;
  std::filesystem::create_directories(cli_options.user_data_dir, dir_err);
  if (dir_err) {
    std::cerr << "Warning: failed to ensure user data dir "
              << cli_options.user_data_dir << ": " << dir_err.message()
              << std::endl;
  }

  if (!cli_options.debug_log_path.empty()) {
    rethread::SetDebugLogPath(cli_options.debug_log_path);
  }

  rethread::SetDefaultBackgroundColor(cli_options.background_color);

  CefMainArgs main_args(argc, argv);

#if defined(__linux__)
  SuppressIbusWarnings();
#endif

  int exit_code = CefExecuteProcess(main_args, nullptr, nullptr);
  if (exit_code >= 0) {
    return exit_code;
  }

#if defined(CEF_X11)
  XSetErrorHandler(XErrorHandlerImpl);
  XSetIOErrorHandler(XIOErrorHandlerImpl);
#endif

  CefRefPtr<CefCommandLine> command_line = CefCommandLine::CreateCommandLine();
  command_line->InitFromArgv(argc, argv);

  CefSettings settings;
  CefString(&settings.cache_path).FromString(cli_options.user_data_dir);
  settings.background_color = cli_options.background_color;

#if !defined(CEF_USE_SANDBOX)
  settings.no_sandbox = true;
#endif

  RethreadApp::Options app_options;
  app_options.auto_exit_seconds = cli_options.auto_exit_seconds;
  app_options.tab_socket_path =
      rethread::TabSocketPath(cli_options.user_data_dir);
  CefRefPtr<RethreadApp> app(new RethreadApp(app_options));

  if (!CefInitialize(main_args, settings, app.get(), nullptr)) {
    return CefGetExitCode();
  }

  CefRunMessageLoop();
  TabIpcServer::Get()->Stop();
  TabIpcServer::Get()->Join();
  CefShutdown();
  return 0;
}
