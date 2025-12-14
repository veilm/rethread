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

namespace rethread {
namespace {

void PrintTabUsage() {
  std::cerr
      << "Usage: rethread tabs [--user-data-dir=PATH] <command>\n"
         "Commands:\n"
         "  get|list              List open tabs.\n"
         "  switch <id>           Activate the tab with the given id.\n"
         "  cycle <delta>         Move relative tab focus.\n"
         "  open <url>            Open a new tab with the URL.\n"
         "  close [index]         Close the tab at 1-based index or the active "
         "tab if omitted.\n"
         "\n"
         "Use `rethread bind ...` / `rethread unbind ...` for key bindings and\n"
         "`rethread tabstrip ...` to control the overlay.\n";
}

void PrintBindUsage() {
  std::cerr
      << "Usage: rethread bind [--user-data-dir=PATH] [mods] [--no-consume]\n"
      << "                      --key=K -- command...\n"
      << "Mods:\n"
      << "  --alt --ctrl --shift --command/--meta\n"
      << "Other flags:\n"
      << "  --no-consume          Allow the key event to pass through to the page\n"
      << "  --user-data-dir PATH  Target a specific profile/socket\n";
}

void PrintUnbindUsage() {
  std::cerr
      << "Usage: rethread unbind [--user-data-dir=PATH] [mods] --key=K\n"
      << "Mods:\n"
      << "  --alt --ctrl --shift --command/--meta\n";
}

void PrintTabStripUsage() {
  std::cerr << "Usage: rethread tabstrip [--user-data-dir=PATH] "
               "show|hide|toggle|peek <ms>\n";
}

struct BindingOptions {
  bool alt = false;
  bool ctrl = false;
  bool shift = false;
  bool command = false;
  bool consume = true;
  std::string key;
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
                      std::string* user_data_dir,
                      int* index) {
  for (; *index < argc; ++(*index)) {
    std::string arg = argv[*index];
    const std::string prefix = "--user-data-dir=";
    if (arg.rfind(prefix, 0) == 0) {
      *user_data_dir = arg.substr(prefix.size());
      continue;
    }
    if (arg == "--user-data-dir") {
      if (*index + 1 >= argc) {
        std::cerr << "Missing value after --user-data-dir\n";
        return false;
      }
      *user_data_dir = argv[++(*index)];
      continue;
    }
    break;
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
  shutdown(fd, SHUT_WR);

  char buffer[512];
  ssize_t n = 0;
  while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
    std::cout.write(buffer, n);
  }
  close(fd);
  return true;
}

}  // namespace

std::string TabSocketPath(const std::string& user_data_dir) {
  if (user_data_dir.empty()) {
    return std::string("tabs.sock");
  }
  return user_data_dir + "/tabs.sock";
}

int RunTabCli(int argc, char* argv[], const std::string& default_user_data_dir) {
  std::string user_data_dir = default_user_data_dir;
  int index = 0;
  if (!ParseUserDataDir(argc, argv, &user_data_dir, &index)) {
    return 1;
  }
  if (index < argc) {
    std::string maybe_help = argv[index];
    if (maybe_help == "--help" || maybe_help == "-h") {
      PrintBindUsage();
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
  } else if (cmd == "open") {
    if (index >= argc) {
      std::cerr << "open requires a URL\n";
      return 1;
    }
    payload << "open ";
    for (int i = index; i < argc; ++i) {
      if (i > index) {
        payload << " ";
      }
      payload << argv[i];
    }
    payload << "\n";
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
  std::string user_data_dir = default_user_data_dir;
  int index = 0;
  if (!ParseUserDataDir(argc, argv, &user_data_dir, &index)) {
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

  if (options.key.empty()) {
    std::cerr << "bind requires --key\n";
    PrintBindUsage();
    return 1;
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
  payload << " -- " << command_stream.str() << "\n";

  if (!SendCommand(TabSocketPath(user_data_dir), payload.str())) {
    return 1;
  }
  return 0;
}

int RunUnbindCli(int argc,
                 char* argv[],
                 const std::string& default_user_data_dir) {
  std::string user_data_dir = default_user_data_dir;
  int index = 0;
  if (!ParseUserDataDir(argc, argv, &user_data_dir, &index)) {
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
  if (options.key.empty()) {
    std::cerr << "unbind requires --key\n";
    PrintUnbindUsage();
    return 1;
  }
  if (index < argc) {
    std::cerr << "unbind does not accept extra arguments\n";
    PrintUnbindUsage();
    return 1;
  }

  std::ostringstream payload;
  payload << "unbind";
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
  payload << " --key=" << options.key << "\n";

  if (!SendCommand(TabSocketPath(user_data_dir), payload.str())) {
    return 1;
  }
  return 0;
}

int RunTabStripCli(int argc,
                   char* argv[],
                   const std::string& default_user_data_dir) {
  std::string user_data_dir = default_user_data_dir;
  int index = 0;
  if (!ParseUserDataDir(argc, argv, &user_data_dir, &index)) {
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

}  // namespace rethread
