#include "app/app.h"

#include <cctype>
#include <cstdlib>
#include <iostream>

#include <QApplication>
#include <QColor>
#include <QGuiApplication>
#include <QPalette>
#include <QRegularExpression>
#include <QStyle>
#include <QStyleHints>
#include <QString>

#include "app/tab_cli.h"
#include "app/user_dirs.h"
#include "common/theme.h"

namespace {

struct CliOptions {
  bool show_help = false;
  std::string user_data_dir;
  std::string debug_log_path;
  std::string color_scheme = "dark";
  std::string initial_url = "https://veilm.github.io/rethread/";
  std::string startup_script_path;
  std::string profile_name = rethread::kDefaultProfileName;
  bool profile_specified = false;
  int auto_exit_seconds = 0;
  uint32_t background_color = rethread::kDefaultBackgroundColor;
  bool user_data_dir_overridden = false;
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
      options.user_data_dir_overridden = true;
      continue;
    }
    if (arg == "--user-data-dir" && i + 1 < argc) {
      options.user_data_dir = argv[++i];
      options.user_data_dir_overridden = true;
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

    const std::string profile_prefix = "--profile=";
    if (arg.rfind(profile_prefix, 0) == 0) {
      options.profile_name = arg.substr(profile_prefix.size());
      options.profile_specified = true;
      continue;
    }
    if (arg == "--profile" && i + 1 < argc) {
      options.profile_name = argv[++i];
      options.profile_specified = true;
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

    const std::string scheme_prefix = "--color-scheme=";
    if (arg.rfind(scheme_prefix, 0) == 0) {
      options.color_scheme = arg.substr(scheme_prefix.size());
      continue;
    }
    if (arg == "--color-scheme" && i + 1 < argc) {
      options.color_scheme = argv[++i];
      continue;
    }
  }
  if (!options.user_data_dir_overridden) {
    if (options.profile_specified) {
      std::string profile = options.profile_name;
      if (profile.empty()) {
        profile = rethread::kDefaultProfileName;
      }
      const std::string root = rethread::DefaultUserDataRoot();
      if (root.empty()) {
        options.user_data_dir = profile;
      } else if (!root.empty() &&
                 (root.back() == '/' || root.back() == '\\')) {
        options.user_data_dir = root + profile;
      } else {
        options.user_data_dir = root + "/" + profile;
      }
    } else {
      const char* env_dir = std::getenv("RETHREAD_USER_DATA_DIR");
      if (env_dir && env_dir[0] != '\0') {
        options.user_data_dir = env_dir;
      } else {
        options.user_data_dir = rethread::DefaultUserDataDir();
      }
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
      << "                          $XDG_DATA_HOME/rethread/PROFILE).\n"
      << "  --profile=NAME          Use a profile subdirectory NAME (default:\n"
      << "                          " << rethread::kDefaultProfileName
      << ").\n"
      << "  --background-color=HEX  Default background color in #RRGGBB or\n"
      << "                          #AARRGGBB format.\n"
      << "  --url=URL               Initial page to load (defaults to\n"
      << "                          https://veilm.github.io/rethread/).\n"
      << "  --debug-log=PATH        Append debug output to PATH.\n"
      << "  --auto-exit=SECONDS     Quit automatically after SECONDS.\n"
      << "  --startup-script=PATH   Run PATH after launch (defaults to\n"
      << "                          $XDG_CONFIG_HOME/rethread/startup.sh).\n"
      << "  --color-scheme=SCHEME   Force auto, light, or dark (default: dark).\n";
  std::cout << "\nEnvironment:\n"
            << "  RETHREAD_USER_DATA_DIR  Default profile directory when no flags\n"
            << "                          override it.\n";
}

QColor ColorFromRgba(uint32_t rgba) {
  return QColor::fromRgb((rgba >> 16) & 0xFF, (rgba >> 8) & 0xFF,
                         rgba & 0xFF, (rgba >> 24) & 0xFF);
}

rethread::ColorScheme ParseColorSchemeFlag(const std::string& value) {
  std::string lowered;
  lowered.reserve(value.size());
  for (char c : value) {
    lowered.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(c))));
  }
  if (lowered == "auto") {
    return rethread::ColorScheme::kAuto;
  }
  if (lowered == "light") {
    return rethread::ColorScheme::kLight;
  }
  if (lowered != "dark") {
    std::cerr << "Unknown color scheme '" << value
              << "', defaulting to dark.\n";
  }
  return rethread::ColorScheme::kDark;
}

QStringList SplitChromiumFlags(const QString& flags) {
  return flags.split(QRegularExpression(QStringLiteral("\\s+")),
                     Qt::SkipEmptyParts);
}

QString ComposeChromiumFlags(const QStringList& flag_list) {
  return flag_list.join(QLatin1Char(' '));
}

QMap<QString, QString> ParseBlinkSettings(const QStringList& values) {
  QMap<QString, QString> map;
  for (const QString& entry : values) {
    const int idx = entry.indexOf('=');
    if (idx <= 0) {
      map.insert(entry.trimmed(), QString());
      continue;
    }
    const QString key = entry.left(idx).trimmed();
    const QString value = entry.mid(idx + 1).trimmed();
    map.insert(key, value);
  }
  return map;
}

QString ComposeBlinkSettings(const QMap<QString, QString>& settings) {
  QStringList parts;
  parts.reserve(settings.size());
  for (auto it = settings.cbegin(); it != settings.cend(); ++it) {
    if (it.value().isEmpty()) {
      parts << it.key();
    } else {
      parts << QStringLiteral("%1=%2").arg(it.key(), it.value());
    }
  }
  return parts.join(QLatin1Char(','));
}

void ApplyChromiumColorPreference(rethread::ColorScheme scheme) {
  const QByteArray env = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
  QStringList flags = SplitChromiumFlags(QString::fromUtf8(env));
  QStringList blink_entries;
  QStringList other_flags;
  const QString blink_prefix = QStringLiteral("--blink-settings=");
  for (const QString& flag : flags) {
    if (flag.startsWith(blink_prefix)) {
      const QString payload = flag.mid(blink_prefix.size());
      if (!payload.isEmpty()) {
        blink_entries << payload.split(QLatin1Char(','), Qt::SkipEmptyParts);
      }
    } else {
      other_flags << flag;
    }
  }

  QMap<QString, QString> settings = ParseBlinkSettings(blink_entries);
  switch (scheme) {
    case rethread::ColorScheme::kLight:
      settings.insert(QStringLiteral("preferredColorScheme"), QStringLiteral("1"));
      break;
    case rethread::ColorScheme::kDark:
      settings.insert(QStringLiteral("preferredColorScheme"), QStringLiteral("0"));
      break;
    case rethread::ColorScheme::kAuto:
      settings.remove(QStringLiteral("preferredColorScheme"));
      break;
  }

  if (!settings.isEmpty()) {
    const QString merged =
        QStringLiteral("--blink-settings=%1").arg(ComposeBlinkSettings(settings));
    other_flags << merged;
  }

  QString final_flags = ComposeChromiumFlags(other_flags);
  qputenv("QTWEBENGINE_CHROMIUM_FLAGS", final_flags.toUtf8());
}

void ApplyQtPalette(QApplication& app, rethread::ColorScheme scheme) {
  if (auto* hints = QGuiApplication::styleHints()) {
    switch (scheme) {
      case rethread::ColorScheme::kLight:
        hints->setColorScheme(Qt::ColorScheme::Light);
        break;
      case rethread::ColorScheme::kDark:
        hints->setColorScheme(Qt::ColorScheme::Dark);
        break;
      case rethread::ColorScheme::kAuto:
        hints->unsetColorScheme();
        break;
    }
  }

  if (scheme != rethread::ColorScheme::kDark) {
    app.setPalette(app.style()->standardPalette());
    return;
  }
  QPalette palette;
  palette.setColor(QPalette::Window, QColor(18, 18, 18));
  palette.setColor(QPalette::WindowText, Qt::white);
  palette.setColor(QPalette::Base, QColor(25, 25, 25));
  palette.setColor(QPalette::AlternateBase, QColor(35, 35, 35));
  palette.setColor(QPalette::Text, Qt::white);
  palette.setColor(QPalette::Button, QColor(35, 35, 35));
  palette.setColor(QPalette::ButtonText, Qt::white);
  palette.setColor(QPalette::Highlight, QColor(45, 140, 240));
  palette.setColor(QPalette::HighlightedText, Qt::black);
  app.setPalette(palette);
}

}  // namespace

int main(int argc, char* argv[]) {
  CliOptions cli = ParseCliOptions(argc, argv);
  if (cli.show_help) {
    PrintHelp();
    return 0;
  }

  rethread::ColorScheme scheme = ParseColorSchemeFlag(cli.color_scheme);
  ApplyChromiumColorPreference(scheme);
  QApplication app(argc, argv);
  ApplyQtPalette(app, scheme);

  rethread::BrowserOptions options;
  options.user_data_dir = QString::fromStdString(cli.user_data_dir);
  options.initial_url = QString::fromStdString(cli.initial_url);
  options.startup_script_path = QString::fromStdString(cli.startup_script_path);
  options.debug_log_path = QString::fromStdString(cli.debug_log_path);
  options.tab_socket_path =
      QString::fromStdString(rethread::TabSocketPath(cli.user_data_dir));
  options.auto_exit_seconds = cli.auto_exit_seconds;
  options.background_color = ColorFromRgba(cli.background_color);
  options.color_scheme = scheme;

  rethread::BrowserApplication browser(options);
  if (!browser.Initialize()) {
    return 1;
  }

  return app.exec();
}
