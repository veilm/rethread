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

}  // namespace rethread
