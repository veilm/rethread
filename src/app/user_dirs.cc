#include "app/user_dirs.h"

#include <cstdlib>
#include <string>

namespace {

std::string GetEnv(const char* key) {
  const char* value = std::getenv(key);
  if (!value) {
    return std::string();
  }
  return std::string(value);
}

}  // namespace

namespace rethread {

std::string DefaultUserDataRoot() {
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

std::string DefaultUserDataDir() {
  std::string root = DefaultUserDataRoot();
  if (root.empty()) {
    return kDefaultProfileName;
  }
  return root + "/" + kDefaultProfileName;
}

std::string DefaultConfigDir() {
  std::string base = GetEnv("XDG_CONFIG_HOME");
  if (base.empty()) {
    std::string home = GetEnv("HOME");
    if (home.empty()) {
      return ".config/rethread";
    }
    base = home + "/.config";
  }
  return base + "/rethread";
}

std::string DefaultStartupScriptPath() {
  return DefaultConfigDir() + "/startup.sh";
}

std::string CdpPortPath(const std::string& user_data_dir) {
  if (user_data_dir.empty()) {
    return "cdp-port.txt";
  }
  return user_data_dir + "/cdp-port.txt";
}

}  // namespace rethread
