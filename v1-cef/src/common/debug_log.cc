#include "common/debug_log.h"

#include <chrono>
#include <fstream>
#include <mutex>

namespace rethread {
namespace {
std::string* GetLogPath() {
  static std::string path;
  return &path;
}

std::mutex& GetLogMutex() {
  static std::mutex mtx;
  return mtx;
}

std::string Timestamp() {
  using std::chrono::system_clock;
  auto now = system_clock::now();
  auto secs = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
  return std::to_string(secs);
}
}  // namespace

void SetDebugLogPath(const std::string& path) {
  std::lock_guard<std::mutex> lock(GetLogMutex());
  *GetLogPath() = path;
}

void AppendDebugLog(const std::string& message) {
  std::lock_guard<std::mutex> lock(GetLogMutex());
  const std::string& path = *GetLogPath();
  if (path.empty()) {
    return;
  }
  std::ofstream file(path, std::ios::app);
  if (!file.is_open()) {
    return;
  }
  file << "[" << Timestamp() << "] " << message << std::endl;
}

}  // namespace rethread
