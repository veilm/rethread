#ifndef RETHREAD_COMMON_DEBUG_LOG_H_
#define RETHREAD_COMMON_DEBUG_LOG_H_

#include <string>

namespace rethread {

void SetDebugLogPath(const std::string& path);
void AppendDebugLog(const std::string& message);

}  // namespace rethread

#endif  // RETHREAD_COMMON_DEBUG_LOG_H_
