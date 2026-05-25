#include "session_manager.h"

namespace nook {
namespace comm {

Session* SessionManager::CreateSession(std::unique_ptr<Transport> transport) {
    if (transport == nullptr) {
        return nullptr;
    }

    auto session = std::make_unique<Session>(std::move(transport));
    Session* raw = session.get();

    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.emplace(raw->GetId(), std::move(session));
    return raw;
}

Session* SessionManager::GetSession(uint32_t session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    return it != sessions_.end() ? it->second.get() : nullptr;
}

Session* SessionManager::GetSessionByPid(int pid) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : sessions_) {
        if (entry.second != nullptr && entry.second->GetPeerPid() == pid) {
            return entry.second.get();
        }
    }
    return nullptr;
}

void SessionManager::RemoveSession(uint32_t session_id) {
    std::unique_ptr<Session> removed;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) {
            return;
        }
        removed = std::move(it->second);
        sessions_.erase(it);
    }

    if (removed != nullptr) {
        removed->Stop();
        std::lock_guard<std::mutex> lock(mutex_);
        retired_sessions_.push_back(std::move(removed));
    }
}

void SessionManager::Clear() {
    std::unordered_map<uint32_t, std::unique_ptr<Session>> removed;
    std::vector<std::unique_ptr<Session>> retired;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        removed = std::move(sessions_);
        sessions_.clear();
        retired = std::move(retired_sessions_);
        retired_sessions_.clear();
    }

    for (auto& entry : removed) {
        if (entry.second != nullptr) {
            entry.second->Stop();
        }
    }
    for (auto& session : retired) {
        if (session != nullptr) {
            session->Stop();
        }
    }
}

std::vector<Session*> SessionManager::GetAllSessions() {
    std::vector<Session*> result;
    std::lock_guard<std::mutex> lock(mutex_);
    result.reserve(sessions_.size());
    for (auto& entry : sessions_) {
        if (entry.second != nullptr) {
            result.push_back(entry.second.get());
        }
    }
    return result;
}

}  // namespace comm
}  // namespace nook
