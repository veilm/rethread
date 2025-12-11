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
  std::cerr << "Usage: rethread tabs [--user-data-dir=PATH] <command>\n"
               "Commands:\n"
               "  get|list              List open tabs.\n"
               "  switch <id>           Activate the tab with the given id.\n"
               "  open <url>            Open a new tab with the URL.\n";
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

}  // namespace rethread
