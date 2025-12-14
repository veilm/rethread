#include <limits.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "app/tab_cli.h"
#include "app/user_dirs.h"

namespace {

void PrintCliUsage() {
  std::cout << "Usage:\n"
            << "  rethread tabs [--user-data-dir=PATH] [--profile=NAME] <command>\n"
            << "    Interact with a running instance (list, switch, cycle, open ...).\n"
            << "  rethread eval [--user-data-dir=PATH] [--profile=NAME] [--stdin]\n"
            << "                [--tab-id=N|--tab-index=N] <script>\n"
            << "    Evaluate JavaScript in a tab and print the JSON-encoded result.\n"
            << "  rethread bind [--user-data-dir=PATH] [--profile=NAME]\n"
            << "                [mods] --key=K -- command\n"
            << "    Register a key binding that runs `command`.\n"
            << "  rethread unbind [--user-data-dir=PATH] [--profile=NAME]\n"
            << "                  [mods] --key=K\n"
            << "    Remove the matching key binding.\n"
            << "  rethread tabstrip [--user-data-dir=PATH] [--profile=NAME]\n"
            << "                    show|hide|toggle|peek <ms>\n"
            << "    Control the tab strip overlay.\n"
            << "  rethread browser [options]\n"
            << "    Launch the browser UI (same flags as rethread-browser).\n";
}

std::string ResolveBrowserBinary(const char* argv0) {
  char buffer[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  std::filesystem::path self_path;
  if (len > 0) {
    buffer[len] = '\0';
    self_path = std::filesystem::path(buffer);
  } else if (argv0 && argv0[0] == '/') {
    self_path = std::filesystem::path(argv0);
  }

  if (!self_path.empty()) {
    std::filesystem::path candidate =
        self_path.parent_path() / "rethread-browser";
    if (std::filesystem::exists(candidate)) {
      return candidate.string();
    }
  }
  return "rethread-browser";
}

int ExecBrowser(int argc, char* argv[]) {
  std::string browser = ResolveBrowserBinary(argv[0]);
  std::vector<char*> exec_argv;
  exec_argv.reserve(static_cast<size_t>(argc));
  exec_argv.push_back(const_cast<char*>(browser.c_str()));
  for (int i = 2; i < argc; ++i) {
    exec_argv.push_back(argv[i]);
  }
  exec_argv.push_back(nullptr);

  if (execv(browser.c_str(), exec_argv.data()) != 0) {
    std::perror("rethread");
    return 1;
  }
  return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    PrintCliUsage();
    return 1;
  }

  std::string command = argv[1];
  if (command == "tabs") {
    return rethread::RunTabCli(argc - 2, argv + 2,
                               rethread::DefaultUserDataRoot());
  }
  if (command == "bind") {
    return rethread::RunBindCli(argc - 2, argv + 2,
                                rethread::DefaultUserDataRoot());
  }
  if (command == "unbind") {
    return rethread::RunUnbindCli(argc - 2, argv + 2,
                                  rethread::DefaultUserDataRoot());
  }
  if (command == "tabstrip") {
    return rethread::RunTabStripCli(argc - 2, argv + 2,
                                    rethread::DefaultUserDataRoot());
  }
  if (command == "eval") {
    return rethread::RunEvalCli(argc - 2, argv + 2,
                                rethread::DefaultUserDataRoot());
  }

  if (command == "browser") {
    if (argc >= 3) {
      std::string next = argv[2];
      if (next == "tabs" || next == "bind" || next == "unbind" ||
          next == "tabstrip") {
        std::cerr << "`rethread browser " << next
                  << "` is not supported. Use `rethread " << next << " ...` "
                  << "instead.\n";
        return 1;
      }
    }
    return ExecBrowser(argc, argv);
  }

  if (command == "--help" || command == "-h") {
    PrintCliUsage();
    return 0;
  }

  std::cerr << "Unknown command: " << command << "\n";
  PrintCliUsage();
  return 1;
}
