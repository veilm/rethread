#ifndef RETHREAD_APP_TAB_CLI_H_
#define RETHREAD_APP_TAB_CLI_H_

#include <string>

namespace rethread {

int RunTabCli(int argc, char* argv[], const std::string& default_user_data_dir);
int RunBindCli(int argc, char* argv[], const std::string& default_user_data_dir);
int RunUnbindCli(int argc, char* argv[], const std::string& default_user_data_dir);
int RunTabStripCli(int argc, char* argv[],
                   const std::string& default_user_data_dir);
int RunEvalCli(int argc, char* argv[], const std::string& default_user_data_dir);
int RunRulesCli(int argc, char* argv[], const std::string& default_user_data_dir);

std::string TabSocketPath(const std::string& user_data_dir);

}  // namespace rethread

#endif  // RETHREAD_APP_TAB_CLI_H_
