#include "app/app.h"

#include <cctype>
#include <cstdlib>
#include <iostream>

#include <QApplication>
#include <QColor>

#include "app/tab_cli.h"
#include "app/user_dirs.h"
#include "common/theme.h"

namespace {

struct CliOptions {
  bool show_help = false;
  std::string user_data_dir;
  std::string debug_log_path;
  std::string initial_url = "https://veilm.github.io/rethread/";
  std::string startup_script_path;
  int auto_exit_seconds = 0;
  uint32_t background_color = rethread::kDefaultBackgroundColor;
};

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
  options.user_data_dir = rethread::DefaultUserDataDir();
  options.startup_script_path = rethread::DefaultStartupScriptPath();

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
      uint32_t parsed = 0;
      if (ParseColorValue(arg.substr(color_prefix.size()), &parsed)) {
        options.background_color = parsed;
      } else {
        std::cerr << "Ignoring invalid --background-color value: "
                  << arg.substr(color_prefix.size()) << "\n";
      }
      continue;
    }
    if (arg == "--background-color" && i + 1 < argc) {
      uint32_t parsed = 0;
      if (ParseColorValue(argv[i + 1], &parsed)) {
        options.background_color = parsed;
      } else {
        std::cerr << "Ignoring invalid --background-color value: "
                  << argv[i + 1] << "\n";
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

    const std::string url_prefix = "--url=";
    if (arg.rfind(url_prefix, 0) == 0) {
      options.initial_url = arg.substr(url_prefix.size());
      continue;
    }
    if (arg == "--url" && i + 1 < argc) {
      options.initial_url = argv[++i];
      continue;
    }

    const std::string startup_prefix = "--startup-script=";
    if (arg.rfind(startup_prefix, 0) == 0) {
      options.startup_script_path = arg.substr(startup_prefix.size());
      continue;
    }
    if (arg == "--startup-script" && i + 1 < argc) {
      options.startup_script_path = argv[++i];
      continue;
    }
  }
  return options;
}

void PrintHelp() {
  std::cout
      << "Usage:\n"
      << "  rethread browser [options]\n"
      << "\n"
      << "Options:\n"
      << "  --help, -h              Show this help and exit.\n"
      << "  --user-data-dir=PATH    Override the profile directory (defaults to\n"
      << "                          $XDG_DATA_HOME/rethread).\n"
      << "  --background-color=HEX  Default background color in #RRGGBB or\n"
      << "                          #AARRGGBB format.\n"
      << "  --url=URL               Initial page to load (defaults to\n"
      << "                          https://veilm.github.io/rethread/).\n"
      << "  --debug-log=PATH        Append debug output to PATH.\n"
      << "  --auto-exit=SECONDS     Quit automatically after SECONDS.\n"
      << "  --startup-script=PATH   Run PATH after launch (defaults to\n"
      << "                          $XDG_CONFIG_HOME/rethread/startup.sh).\n";
}

QColor ColorFromRgba(uint32_t rgba) {
  return QColor::fromRgb((rgba >> 16) & 0xFF, (rgba >> 8) & 0xFF,
                         rgba & 0xFF, (rgba >> 24) & 0xFF);
}

}  // namespace

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  CliOptions cli = ParseCliOptions(argc, argv);
  if (cli.show_help) {
    PrintHelp();
    return 0;
  }

  rethread::BrowserOptions options;
  options.user_data_dir = QString::fromStdString(cli.user_data_dir);
  options.initial_url = QString::fromStdString(cli.initial_url);
  options.startup_script_path = QString::fromStdString(cli.startup_script_path);
  options.debug_log_path = QString::fromStdString(cli.debug_log_path);
  options.tab_socket_path =
      QString::fromStdString(rethread::TabSocketPath(cli.user_data_dir));
  options.auto_exit_seconds = cli.auto_exit_seconds;
  options.background_color = ColorFromRgba(cli.background_color);

  rethread::BrowserApplication browser(options);
  if (!browser.Initialize()) {
    return 1;
  }

  return app.exec();
}
