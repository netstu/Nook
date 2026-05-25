#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace nook {
namespace comm {

class IoLoop {
public:
    using EventCallback = std::function<void(int fd, uint32_t events)>;

    IoLoop();
    ~IoLoop();

    bool Start();
    void Stop();

    bool AddWatch(int fd, uint32_t events, EventCallback callback);
    bool ModifyWatch(int fd, uint32_t events);
    bool RemoveWatch(int fd);

    void Post(std::function<void()> task);
    bool IsRunning() const;

private:
    void Loop();
    void DrainTasks();

#if !defined(_WIN32)
    int epoll_fd_ = -1;
#endif
    std::atomic<bool> running_{false};
    std::thread loop_thread_;

    std::mutex watches_mutex_;
    std::unordered_map<int, EventCallback> watches_;
    std::unordered_map<int, uint32_t> watch_events_;

    std::mutex tasks_mutex_;
    std::vector<std::function<void()>> pending_tasks_;
};

}  // namespace comm
}  // namespace nook
