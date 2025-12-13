#ifndef RETHREAD_BROWSER_TAB_IPC_SERVER_H_
#define RETHREAD_BROWSER_TAB_IPC_SERVER_H_

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace rethread {

class TabIpcServer {
 public:
  static TabIpcServer* Get();

  void Start(const std::string& socket_path);
  void Stop();
  void Join();

 private:
  TabIpcServer();
  ~TabIpcServer();

  void ThreadMain();
  void HandleClient(int client_fd);
  std::string HandleCommand(const std::string& command);
  void NotifyWake();
  void CloseWakePipe();

  std::atomic<int> listen_fd_{-1};
  std::string socket_path_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  bool stop_requested_ = false;
  int wake_pipe_[2] = {-1, -1};
  std::mutex state_mutex_;

  TabIpcServer(const TabIpcServer&) = delete;
  TabIpcServer& operator=(const TabIpcServer&) = delete;
};

}  // namespace rethread

#endif  // RETHREAD_BROWSER_TAB_IPC_SERVER_H_
