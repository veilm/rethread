#include "app/app.h"

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <string>

#include "include/cef_app.h"

#if defined(CEF_X11)
#include <X11/Xlib.h>
#endif

#include "common/theme.h"
#include "include/base/cef_logging.h"
#include "include/cef_command_line.h"

namespace {

struct CliOptions {
  bool show_help = false;
  std::string user_data_dir;
  uint32_t background_color = rethread::kDefaultBackgroundColor;
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
  }

  if (options.user_data_dir.empty()) {
    options.user_data_dir = DefaultUserDataDir();
  }

  return options;
}

void PrintHelp() {
  std::cout << "Usage: rethread [options]\n"
               "\n"
               "Options:\n"
               "  --help, -h            Show this message and exit.\n"
               "  --user-data-dir=PATH  Override the directory used for browser profile\n"
               "                        data (defaults to $XDG_DATA_HOME/rethread or\n"
               "                        $HOME/.local/share/rethread).\n"
               "  --background-color=HEX\n"
               "                        Default background color in #RRGGBB or\n"
               "                        #AARRGGBB format.\n"
               "  --url=URL             Initial page to load (defaults to\n"
               "                        https://github.com/veilm/rethread).\n"
               "  --initial-show-state=(normal|minimized|maximized|hidden)\n"
               "                        Controls the first window's show state.\n";
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

}  // namespace

using rethread::RethreadApp;

NO_STACK_PROTECTOR
int main(int argc, char* argv[]) {
  CliOptions cli_options = ParseCliOptions(argc, argv);
  if (cli_options.show_help) {
    PrintHelp();
    return 0;
  }

  rethread::SetDefaultBackgroundColor(cli_options.background_color);

  CefMainArgs main_args(argc, argv);

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

  CefRefPtr<RethreadApp> app(new RethreadApp);

  if (!CefInitialize(main_args, settings, app.get(), nullptr)) {
    return CefGetExitCode();
  }

  CefRunMessageLoop();
  CefShutdown();
  return 0;
}
