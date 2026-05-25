#include "io_loop.h"

#include <chrono>

#if !defined(_WIN32)
#include <sys/epoll.h>
#include <unistd.h>
#endif

namespace nook {
namespace comm {

IoLoop::IoLoop() = default;

IoLoop::~IoLoop() {
    Stop();
}

bool IoLoop::Start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return true;
    }

#if !defined(_WIN32)
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) {
        running_.store(false, std::memory_order_release);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(watches_mutex_);
        for (const auto& entry : watch_events_) {
            epoll_event ev{};
            ev.events = entry.second;
            ev.data.fd = entry.first;
            if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, entry.first, &ev) != 0) {
                close(epoll_fd_);
                epoll_fd_ = -1;
                running_.store(false, std::memory_order_release);
                return false;
            }
        }
    }
#endif

    loop_thread_ = std::thread(&IoLoop::Loop, this);
    return true;
}

void IoLoop::Stop() {
    const bool was_running = running_.exchange(false, std::memory_order_acq_rel);
    if (!was_running) {
        return;
    }

    if (loop_thread_.joinable()) {
        loop_thread_.join();
    }

#if !defined(_WIN32)
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }
#endif
}

bool IoLoop::AddWatch(int fd, uint32_t events, EventCallback callback) {
    if (fd < 0 || !callback) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(watches_mutex_);
        watches_[fd] = std::move(callback);
        watch_events_[fd] = events;
    }

#if !defined(_WIN32)
    if (epoll_fd_ >= 0) {
        epoll_event ev{};
        ev.events = events;
        ev.data.fd = fd;
        return epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == 0;
    }
#endif
    return true;
}

bool IoLoop::ModifyWatch(int fd, uint32_t events) {
    std::lock_guard<std::mutex> lock(watches_mutex_);
    auto it = watch_events_.find(fd);
    if (it == watch_events_.end()) {
        return false;
    }
    it->second = events;

#if !defined(_WIN32)
    if (epoll_fd_ >= 0) {
        epoll_event ev{};
        ev.events = events;
        ev.data.fd = fd;
        return epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) == 0;
    }
#endif
    return true;
}

bool IoLoop::RemoveWatch(int fd) {
    {
        std::lock_guard<std::mutex> lock(watches_mutex_);
        watches_.erase(fd);
        watch_events_.erase(fd);
    }

#if !defined(_WIN32)
    if (epoll_fd_ >= 0) {
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    }
#endif
    return true;
}

void IoLoop::Post(std::function<void()> task) {
    if (!task) {
        return;
    }
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }
    pending_tasks_.push_back(std::move(task));
}

bool IoLoop::IsRunning() const {
    return running_.load(std::memory_order_acquire);
}

void IoLoop::DrainTasks() {
    std::vector<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        tasks.swap(pending_tasks_);
    }

    for (auto& task : tasks) {
        if (task) {
            task();
        }
    }
}

void IoLoop::Loop() {
    while (running_.load(std::memory_order_acquire)) {
        DrainTasks();

#if !defined(_WIN32)
        epoll_event events[16];
        const int count = epoll_wait(epoll_fd_, events, 16, 50);
        if (count <= 0) {
            continue;
        }

        for (int i = 0; i < count; ++i) {
            EventCallback callback;
            {
                std::lock_guard<std::mutex> lock(watches_mutex_);
                auto it = watches_.find(events[i].data.fd);
                if (it != watches_.end()) {
                    callback = it->second;
                }
            }
            if (callback) {
                callback(events[i].data.fd, events[i].events);
            }
        }
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
#endif
    }

    DrainTasks();
}

}  // namespace comm
}  // namespace nook
