#include "app/tab_cli.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <limits>

#include "app/user_dirs.h"

namespace rethread {
namespace {

void PrintTabUsage() {
  std::cerr
      << "Usage: rethread tabs [--user-data-dir=PATH] [--profile=NAME] <command>\n"
         "Commands:\n"
         "  get|list              List open tabs.\n"
         "  switch <id>           Activate the tab with the given id.\n"
         "  cycle <delta>         Move relative tab focus.\n"
         "  swap <target> [peer]  Swap/move tabs by index or +/- offset (wraps around).\n"
         "  open [--at-end] <url> Open a new tab (default inserts after the active tab).\n"
         "  history-back          Navigate back in the active tab.\n"
         "  history-forward       Navigate forward in the active tab.\n"
         "  close [index]         Close the tab at 1-based index or the active "
         "tab if omitted.\n"
         "\n"
         "Use `rethread bind ...` / `rethread unbind ...` for key bindings and\n"
         "`rethread tabstrip ...` to control the overlay.\n";
}

void PrintBindUsage() {
  std::cerr
      << "Usage: rethread bind [--user-data-dir=PATH] [--profile=NAME]\n"
      << "                     [mods] [--no-consume]\n"
      << "                      --key=K -- command...\n"
      << "Mods:\n"
      << "  --alt --ctrl --shift --command/--meta\n"
      << "Other flags:\n"
      << "  --context-menu       Bind right-clicks to run `command`\n"
      << "  --no-consume          Allow the key event to pass through to the page\n"
      << "  --user-data-dir PATH  Target a specific profile/socket\n";
}

void PrintUnbindUsage() {
  std::cerr
      << "Usage: rethread unbind [--user-data-dir=PATH] [--profile=NAME]\n"
      << "                       [mods] --key=K\n"
      << "Mods:\n"
      << "  --alt --ctrl --shift --command/--meta\n"
      << "Other flags:\n"
      << "  --context-menu       Clear the right-click binding\n";
}

void PrintTabStripUsage() {
  std::cerr
      << "Usage: rethread tabstrip [--user-data-dir=PATH] [--profile=NAME]\n"
      << "       show|hide|toggle|peek <ms>\n"
      << "       message --duration=MS [--stdin] <text>\n";
}

void PrintRulesUsage() {
  std::cerr
      << "Usage: rethread rules [--user-data-dir=PATH] [--profile=NAME]\n"
      << "                      (js|iframes) (--whitelist|--blacklist)\n"
      << "                      [--append]\n"
      << "  Provide newline-delimited hostnames via stdin "
         "(e.g. `rethread rules js --blacklist < hosts.txt`).\n";
}

void PrintDevToolsUsage() {
  std::cerr
      << "Usage: rethread devtools [--user-data-dir=PATH] [--profile=NAME] open\n";
}
void PrintEvalUsage() {
  std::cerr
      << "Usage: rethread eval [--user-data-dir=PATH] [--profile=NAME] [--stdin]\n"
      << "                     [--tab-id=N|--tab-index=N] <script>\n"
      << "Options:\n"
      << "  --stdin              Read the script from stdin instead of argv\n"
      << "  --tab-id=N           Target a specific tab id (default: active tab)\n"
      << "  --tab-index=N        Target the 1-based tab index\n";
}

struct BindingOptions {
  bool alt = false;
  bool ctrl = false;
  bool shift = false;
  bool command = false;
  bool consume = true;
  std::string key;
  bool context_menu = false;
};

bool ParseBindingOptions(int argc,
                         char* argv[],
                         int* index,
                         BindingOptions* options,
                         bool allow_consume,
                         bool* encountered_separator) {
  if (!options) {
    return false;
  }
  bool separator = false;
  while (*index < argc) {
    std::string arg = argv[*index];
    if (arg == "--") {
      separator = true;
      ++(*index);
      break;
    }
    if (arg == "--alt") {
      options->alt = true;
      ++(*index);
      continue;
    }
    if (arg == "--ctrl") {
      options->ctrl = true;
      ++(*index);
      continue;
    }
    if (arg == "--shift") {
      options->shift = true;
      ++(*index);
      continue;
    }
    if (arg == "--command" || arg == "--meta") {
      options->command = true;
      ++(*index);
      continue;
    }
    if (allow_consume && arg == "--no-consume") {
      options->consume = false;
      ++(*index);
      continue;
    }
    if (arg == "--context-menu" || arg == "--right-click") {
      options->context_menu = true;
      ++(*index);
      continue;
    }
    const std::string key_prefix = "--key=";
    if (arg.rfind(key_prefix, 0) == 0) {
      options->key = arg.substr(key_prefix.size());
      ++(*index);
      continue;
    }
    if (arg == "--key") {
      if (*index + 1 >= argc) {
        std::cerr << "--key requires a value\n";
        return false;
      }
      options->key = argv[*index + 1];
      *index += 2;
      continue;
    }
    break;
  }
  if (encountered_separator) {
    *encountered_separator = separator;
  }
  return true;
}

bool ParseUserDataDir(int argc,
                      char* argv[],
                      const std::string& default_root,
                      std::string* user_data_dir,
                      int* index) {
  if (!user_data_dir || !index) {
    return false;
  }
  bool user_data_override = false;
  bool profile_specified = false;
  std::string profile_name = rethread::kDefaultProfileName;
  const char* env_dir = std::getenv("RETHREAD_USER_DATA_DIR");
  const std::string env_user_data_dir =
      (env_dir && env_dir[0] != '\0') ? std::string(env_dir) : std::string();
  for (; *index < argc; ++(*index)) {
    std::string arg = argv[*index];
    const std::string prefix = "--user-data-dir=";
    if (arg.rfind(prefix, 0) == 0) {
      *user_data_dir = arg.substr(prefix.size());
      user_data_override = true;
      continue;
    }
    if (arg == "--user-data-dir") {
      if (*index + 1 >= argc) {
        std::cerr << "Missing value after --user-data-dir\n";
        return false;
      }
      *user_data_dir = argv[++(*index)];
      user_data_override = true;
      continue;
    }
    const std::string profile_prefix = "--profile=";
    if (arg.rfind(profile_prefix, 0) == 0) {
      profile_name = arg.substr(profile_prefix.size());
      profile_specified = true;
      continue;
    }
    if (arg == "--profile") {
      if (*index + 1 >= argc) {
        std::cerr << "Missing value after --profile\n";
        return false;
      }
      profile_name = argv[++(*index)];
      profile_specified = true;
      continue;
    }
    break;
  }
  if (!user_data_override) {
    if (profile_specified) {
      std::string profile = profile_name.empty() ? rethread::kDefaultProfileName
                                                 : profile_name;
      if (default_root.empty()) {
        *user_data_dir = profile;
      } else if (default_root.back() == '/' || default_root.back() == '\\') {
        *user_data_dir = default_root + profile;
      } else {
        *user_data_dir = default_root + "/" + profile;
      }
    } else if (!env_user_data_dir.empty()) {
      *user_data_dir = env_user_data_dir;
    } else {
      std::string profile = rethread::kDefaultProfileName;
      if (default_root.empty()) {
        *user_data_dir = profile;
      } else if (default_root.back() == '/' || default_root.back() == '\\') {
        *user_data_dir = default_root + profile;
      } else {
        *user_data_dir = default_root + "/" + profile;
      }
    }
  }
  return true;
}

bool SendCommand(const std::string& socket_path, const std::string& payload) {
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    std::cerr << "Failed to create socket: " << std::strerror(errno) << "\n";
    return false;
  }

  sockaddr_un addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s",
                socket_path.c_str());

  if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    std::cerr << "Failed to connect to " << socket_path << ": "
              << std::strerror(errno) << "\n";
    close(fd);
    return false;
  }

  if (write(fd, payload.data(), payload.size()) < 0) {
    std::cerr << "Failed to send command: " << std::strerror(errno) << "\n";
    close(fd);
    return false;
  }

  char buffer[512];
  ssize_t n = 0;
  while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
    std::cout.write(buffer, n);
  }
  close(fd);
  return true;
}

bool ParsePositiveInt(const std::string& text, int* value) {
  if (!value) {
    return false;
  }
  errno = 0;
  char* end = nullptr;
  long parsed = std::strtol(text.c_str(), &end, 10);
  if (errno != 0 || !end || *end != '\0' || parsed <= 0 ||
      parsed > std::numeric_limits<int>::max()) {
    return false;
  }
  *value = static_cast<int>(parsed);
  return true;
}

std::string HexEncode(const std::string& input) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string output;
  output.reserve(input.size() * 2);
  for (unsigned char c : input) {
    output.push_back(kHex[(c >> 4) & 0xF]);
    output.push_back(kHex[c & 0xF]);
  }
  return output;
}

}  // namespace

std::string TabSocketPath(const std::string& user_data_dir) {
  if (user_data_dir.empty()) {
    return std::string("tabs.sock");
  }
  return user_data_dir + "/tabs.sock";
}

int RunTabCli(int argc, char* argv[], const std::string& default_user_data_dir) {
  std::string user_data_dir;
  int index = 0;
  if (!ParseUserDataDir(argc, argv, default_user_data_dir, &user_data_dir, &index)) {
    return 1;
  }
  if (index < argc) {
    std::string maybe_help = argv[index];
    if (maybe_help == "--help" || maybe_help == "-h") {
      PrintTabUsage();
      return 0;
    }
  }
  if (index >= argc) {
    PrintTabUsage();
    return 1;
  }

  std::string cmd = argv[index++];
  std::ostringstream payload;

  if (cmd == "get" || cmd == "list") {
    payload << "list\n";
  } else if (cmd == "switch") {
    if (index >= argc) {
      std::cerr << "switch requires a tab id\n";
      return 1;
    }
    payload << "switch " << argv[index++] << "\n";
  } else if (cmd == "cycle") {
    if (index >= argc) {
      std::cerr << "cycle requires a delta\n";
      return 1;
    }
    payload << "cycle " << argv[index++] << "\n";
  } else if (cmd == "swap") {
    if (index >= argc) {
      std::cerr << "swap requires at least one index or offset\n";
      return 1;
    }
    payload << "swap";
    for (int i = index; i < argc; ++i) {
      payload << " " << argv[i];
    }
    payload << "\n";
  } else if (cmd == "open") {
    bool open_at_end = false;
    while (index < argc) {
      std::string arg = argv[index];
      if (arg == "--at-end") {
        open_at_end = true;
        ++index;
        continue;
      }
      if (arg == "--") {
        ++index;
        break;
      }
      break;
    }
    if (index >= argc) {
      std::cerr << "open requires a URL\n";
      return 1;
    }
    std::ostringstream url_stream;
    for (int i = index; i < argc; ++i) {
      if (i > index) {
        url_stream << " ";
      }
      url_stream << argv[i];
    }
    const std::string url_text = url_stream.str();
    if (url_text.empty()) {
      std::cerr << "open requires a URL\n";
      return 1;
    }
    payload << "open";
    if (open_at_end) {
      payload << " --at-end";
    }
    payload << " -- " << url_text << "\n";
  } else if (cmd == "history-back") {
    payload << "history-back\n";
  } else if (cmd == "history-forward") {
    payload << "history-forward\n";
  } else if (cmd == "close") {
    payload << "close";
    if (index < argc) {
      payload << " " << argv[index++];
      if (index < argc) {
        std::cerr << "close accepts at most one tab index\n";
        return 1;
      }
    }
    payload << "\n";
  } else {
    std::cerr << "Unknown tabs command: " << cmd << "\n";
    PrintTabUsage();
    return 1;
  }

  const std::string socket_path = TabSocketPath(user_data_dir);
  if (!SendCommand(socket_path, payload.str())) {
    return 1;
  }
  return 0;
}

int RunBindCli(int argc,
               char* argv[],
               const std::string& default_user_data_dir) {
  std::string user_data_dir;
  int index = 0;
  if (!ParseUserDataDir(argc, argv, default_user_data_dir, &user_data_dir, &index)) {
    return 1;
  }
  if (index < argc) {
    std::string maybe_help = argv[index];
    if (maybe_help == "--help" || maybe_help == "-h") {
      PrintBindUsage();
      return 0;
    }
  }

  BindingOptions options;
  if (!ParseBindingOptions(argc, argv, &index, &options, true, nullptr)) {
    PrintBindUsage();
    return 1;
  }

  if (options.context_menu) {
    if (!options.key.empty() || !options.consume || options.alt ||
        options.ctrl || options.shift || options.command) {
      std::cerr << "--context-menu cannot be combined with key or modifier flags\n";
      PrintBindUsage();
      return 1;
    }
  } else {
    if (options.key.empty()) {
      std::cerr << "bind requires --key\n";
      PrintBindUsage();
      return 1;
    }
  }
  if (index >= argc) {
    std::cerr << "bind requires a command\n";
    PrintBindUsage();
    return 1;
  }

  std::ostringstream command_stream;
  for (int i = index; i < argc; ++i) {
    if (i > index) {
      command_stream << " ";
    }
    command_stream << argv[i];
  }

  std::ostringstream payload;
  payload << "bind";
  if (options.context_menu) {
    payload << " --context-menu";
  } else {
    if (options.alt) {
      payload << " --alt";
    }
    if (options.ctrl) {
      payload << " --ctrl";
    }
    if (options.shift) {
      payload << " --shift";
    }
    if (options.command) {
      payload << " --command";
    }
    if (!options.consume) {
      payload << " --no-consume";
    }
    payload << " --key=" << options.key;
  }
  payload << " -- " << command_stream.str() << "\n";

  if (!SendCommand(TabSocketPath(user_data_dir), payload.str())) {
    return 1;
  }
  return 0;
}

int RunUnbindCli(int argc,
                 char* argv[],
                 const std::string& default_user_data_dir) {
  std::string user_data_dir;
  int index = 0;
  if (!ParseUserDataDir(argc, argv, default_user_data_dir, &user_data_dir, &index)) {
    return 1;
  }
  if (index < argc) {
    std::string maybe_help = argv[index];
    if (maybe_help == "--help" || maybe_help == "-h") {
      PrintUnbindUsage();
      return 0;
    }
  }

  BindingOptions options;
  bool saw_separator = false;
  if (!ParseBindingOptions(argc, argv, &index, &options, false,
                           &saw_separator)) {
    PrintUnbindUsage();
    return 1;
  }
  if (saw_separator) {
    std::cerr << "unbind does not accept a command\n";
    PrintUnbindUsage();
    return 1;
  }
  if (options.context_menu) {
    if (!options.key.empty() || options.alt || options.ctrl ||
        options.shift || options.command) {
      std::cerr << "--context-menu cannot be combined with key or modifier flags\n";
      PrintUnbindUsage();
      return 1;
    }
  } else {
    if (options.key.empty()) {
      std::cerr << "unbind requires --key\n";
      PrintUnbindUsage();
      return 1;
    }
  }
  if (index < argc) {
    std::cerr << "unbind does not accept extra arguments\n";
    PrintUnbindUsage();
    return 1;
  }

  std::ostringstream payload;
  payload << "unbind";
  if (options.context_menu) {
    payload << " --context-menu";
  } else {
    if (options.alt) {
      payload << " --alt";
    }
    if (options.ctrl) {
      payload << " --ctrl";
    }
    if (options.shift) {
      payload << " --shift";
    }
    if (options.command) {
      payload << " --command";
    }
    payload << " --key=" << options.key;
  }
  payload << "\n";

  if (!SendCommand(TabSocketPath(user_data_dir), payload.str())) {
    return 1;
  }
  return 0;
}

int RunEvalCli(int argc,
               char* argv[],
               const std::string& default_user_data_dir) {
  std::string user_data_dir;
  int index = 0;
  if (!ParseUserDataDir(argc, argv, default_user_data_dir, &user_data_dir, &index)) {
    return 1;
  }

  bool use_stdin = false;
  int tab_id = 0;
  int tab_index = 0;
  while (index < argc) {
    std::string arg = argv[index];
    if (arg == "--help" || arg == "-h") {
      PrintEvalUsage();
      return 0;
    }
    if (arg == "--stdin") {
      use_stdin = true;
      ++index;
      continue;
    }
    if (arg == "--tab-id") {
      if (index + 1 >= argc) {
        std::cerr << "--tab-id requires a value\n";
        return 1;
      }
      if (!ParsePositiveInt(argv[index + 1], &tab_id)) {
        std::cerr << "Invalid --tab-id value\n";
        return 1;
      }
      index += 2;
      continue;
    }
    const std::string tab_id_prefix = "--tab-id=";
    if (arg.rfind(tab_id_prefix, 0) == 0) {
      if (!ParsePositiveInt(arg.substr(tab_id_prefix.size()), &tab_id)) {
        std::cerr << "Invalid --tab-id value\n";
        return 1;
      }
      ++index;
      continue;
    }
    if (arg == "--tab-index") {
      if (index + 1 >= argc) {
        std::cerr << "--tab-index requires a value\n";
        return 1;
      }
      if (!ParsePositiveInt(argv[index + 1], &tab_index)) {
        std::cerr << "Invalid --tab-index value\n";
        return 1;
      }
      index += 2;
      continue;
    }
    const std::string tab_index_prefix = "--tab-index=";
    if (arg.rfind(tab_index_prefix, 0) == 0) {
      if (!ParsePositiveInt(arg.substr(tab_index_prefix.size()), &tab_index)) {
        std::cerr << "Invalid --tab-index value\n";
        return 1;
      }
      ++index;
      continue;
    }
    if (arg == "--") {
      ++index;
      break;
    }
    break;
  }

  if (tab_id > 0 && tab_index > 0) {
    std::cerr << "Specify at most one tab selector (--tab-id or --tab-index)\n";
    return 1;
  }

  std::string script;
  if (use_stdin) {
    if (index < argc) {
      std::cerr << "--stdin cannot be combined with a script argument\n";
      return 1;
    }
    std::ostringstream buffer;
    buffer << std::cin.rdbuf();
    script = buffer.str();
  } else {
    if (index >= argc) {
      std::cerr << "eval requires a script argument\n";
      PrintEvalUsage();
      return 1;
    }
    std::ostringstream buffer;
    for (int i = index; i < argc; ++i) {
      if (i > index) {
        buffer << " ";
      }
      buffer << argv[i];
    }
    script = buffer.str();
  }

  if (script.empty()) {
    std::cerr << "eval requires a non-empty script\n";
    return 1;
  }

  const std::string encoded = HexEncode(script);
  std::ostringstream payload;
  payload << "eval";
  if (tab_id > 0) {
    payload << " --tab-id=" << tab_id;
  }
  if (tab_index > 0) {
    payload << " --tab-index=" << tab_index;
  }
  payload << " --code=" << encoded << "\n";

  if (!SendCommand(TabSocketPath(user_data_dir), payload.str())) {
    return 1;
  }
  return 0;
}

int RunTabStripCli(int argc,
                   char* argv[],
                   const std::string& default_user_data_dir) {
  std::string user_data_dir;
  int index = 0;
  if (!ParseUserDataDir(argc, argv, default_user_data_dir, &user_data_dir, &index)) {
    return 1;
  }
  if (index >= argc) {
    PrintTabStripUsage();
    return 1;
  }
  if (index < argc) {
    std::string maybe_help = argv[index];
    if (maybe_help == "--help" || maybe_help == "-h") {
      PrintTabStripUsage();
      return 0;
    }
  }

  std::string action = argv[index++];
  std::ostringstream payload;
  if (action == "show" || action == "hide" || action == "toggle") {
    if (index < argc) {
      std::cerr << "tabstrip " << action << " does not take extra arguments\n";
      PrintTabStripUsage();
      return 1;
    }
    payload << "tabstrip " << action << "\n";
  } else if (action == "peek") {
    if (index >= argc) {
      std::cerr << "tabstrip peek requires a duration in ms\n";
      PrintTabStripUsage();
      return 1;
    }
    payload << "tabstrip peek " << argv[index++] << "\n";
    if (index < argc) {
      std::cerr << "tabstrip peek only accepts a single duration\n";
      PrintTabStripUsage();
      return 1;
    }
  } else if (action == "message") {
    bool use_stdin = false;
    int duration_ms = -1;
    while (index < argc) {
      std::string arg = argv[index];
      if (arg == "--stdin") {
        use_stdin = true;
        ++index;
        continue;
      }
      if (arg == "--duration") {
        if (index + 1 >= argc) {
          std::cerr << "--duration requires a value\n";
          return 1;
        }
        std::string value = argv[index + 1];
        int parsed = 0;
        if (value != "0" && !ParsePositiveInt(value, &parsed)) {
          std::cerr << "Invalid --duration value\n";
          return 1;
        }
        duration_ms = (value == "0") ? 0 : parsed;
        index += 2;
        continue;
      }
      const std::string duration_prefix = "--duration=";
      if (arg.rfind(duration_prefix, 0) == 0) {
        std::string value = arg.substr(duration_prefix.size());
        int parsed = 0;
        if (value != "0" && !ParsePositiveInt(value, &parsed)) {
          std::cerr << "Invalid --duration value\n";
          return 1;
        }
        duration_ms = (value == "0") ? 0 : parsed;
        ++index;
        continue;
      }
      if (arg == "--") {
        ++index;
        break;
      }
      break;
    }
    if (duration_ms < 0) {
      std::cerr << "tabstrip message requires --duration\n";
      PrintTabStripUsage();
      return 1;
    }
    std::string message;
    if (use_stdin) {
      if (index < argc) {
        std::cerr << "--stdin cannot be combined with inline text\n";
        return 1;
      }
      std::ostringstream buffer;
      buffer << std::cin.rdbuf();
      message = buffer.str();
    } else {
      if (index >= argc) {
        std::cerr << "tabstrip message requires text after --duration\n";
        PrintTabStripUsage();
        return 1;
      }
      std::ostringstream buffer;
      for (int i = index; i < argc; ++i) {
        if (i > index) {
          buffer << " ";
        }
        buffer << argv[i];
      }
      message = buffer.str();
    }
    if (message.empty()) {
      std::cerr << "tabstrip message requires non-empty text\n";
      return 1;
    }
    payload << "tabstrip message --duration=" << duration_ms
            << " --data=" << HexEncode(message) << "\n";
  } else {
    std::cerr << "Unknown tabstrip action: " << action << "\n";
    PrintTabStripUsage();
    return 1;
  }

  if (!SendCommand(TabSocketPath(user_data_dir), payload.str())) {
    return 1;
  }
  return 0;
}

int RunRulesCli(int argc,
                char* argv[],
                const std::string& default_user_data_dir) {
  std::string user_data_dir;
  int index = 0;
  if (!ParseUserDataDir(argc, argv, default_user_data_dir, &user_data_dir,
                        &index)) {
    return 1;
  }
  if (index < argc) {
    std::string maybe_help = argv[index];
    if (maybe_help == "--help" || maybe_help == "-h") {
      PrintRulesUsage();
      return 0;
    }
  }
  if (index >= argc) {
    PrintRulesUsage();
    return 1;
  }
  std::string action = argv[index++];
  if (action != "js" && action != "iframes") {
    std::cerr << "Unknown rules target: " << action << "\n";
    PrintRulesUsage();
    return 1;
  }

  bool whitelist = false;
  bool blacklist = false;
  bool append = false;
  while (index < argc) {
    std::string arg = argv[index];
    if (arg == "--whitelist") {
      whitelist = true;
      ++index;
      continue;
    }
    if (arg == "--blacklist") {
      blacklist = true;
      ++index;
      continue;
    }
    if (arg == "--append") {
      append = true;
      ++index;
      continue;
    }
    if (arg == "--help" || arg == "-h") {
      PrintRulesUsage();
      return 0;
    }
    std::cerr << "Unknown rules flag: " << arg << "\n";
    PrintRulesUsage();
    return 1;
  }
  if (whitelist == blacklist) {
    std::cerr << "Specify exactly one of --whitelist or --blacklist\n";
    PrintRulesUsage();
    return 1;
  }

  std::ostringstream buffer;
  buffer << std::cin.rdbuf();
  const std::string data = buffer.str();
  if (data.empty()) {
    std::cerr << "rules requires host data via stdin\n";
    return 1;
  }
  const std::string encoded = HexEncode(data);
  std::ostringstream payload;
  payload << "rules " << action << " --mode="
          << (whitelist ? "whitelist" : "blacklist") << " --data=" << encoded
          << (append ? " --append" : "") << "\n";
  if (!SendCommand(TabSocketPath(user_data_dir), payload.str())) {
    return 1;
  }
  return 0;
}

int RunDevToolsCli(int argc,
                   char* argv[],
                   const std::string& default_user_data_dir) {
  std::string user_data_dir;
  int index = 0;
  if (!ParseUserDataDir(argc, argv, default_user_data_dir, &user_data_dir,
                        &index)) {
    return 1;
  }
  if (index < argc) {
    std::string maybe_help = argv[index];
    if (maybe_help == "--help" || maybe_help == "-h") {
      PrintDevToolsUsage();
      return 0;
    }
  }
  if (index >= argc) {
    PrintDevToolsUsage();
    return 1;
  }
  std::string action = argv[index++];
  if (action != "open") {
    std::cerr << "Unknown devtools action: " << action << "\n";
    PrintDevToolsUsage();
    return 1;
  }
  if (index < argc) {
    std::cerr << "devtools open does not take additional arguments\n";
    PrintDevToolsUsage();
    return 1;
  }
  if (!SendCommand(TabSocketPath(user_data_dir), "devtools open\n")) {
    return 1;
  }
  return 0;
}

}  // namespace rethread
