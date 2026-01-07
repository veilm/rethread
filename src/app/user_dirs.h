#ifndef RETHREAD_APP_USER_DIRS_H_
#define RETHREAD_APP_USER_DIRS_H_

#include <string>

namespace rethread {

std::string DefaultUserDataDir();
std::string DefaultConfigDir();
std::string DefaultStartupScriptPath();
std::string DefaultUserDataRoot();
std::string CdpPortPath(const std::string& user_data_dir);

inline constexpr char kDefaultProfileName[] = "default";

}  // namespace rethread

#endif  // RETHREAD_APP_USER_DIRS_H_
