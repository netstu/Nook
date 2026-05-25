#pragma once

#include "session.h"

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace nook {
namespace comm {

class SessionManager {
public:
    Session* CreateSession(std::unique_ptr<Transport> transport);
    Session* GetSession(uint32_t session_id);
    Session* GetSessionByPid(int pid);
    void RemoveSession(uint32_t session_id);
    void Clear();
    std::vector<Session*> GetAllSessions();

private:
    std::mutex mutex_;
    std::unordered_map<uint32_t, std::unique_ptr<Session>> sessions_;
    std::vector<std::unique_ptr<Session>> retired_sessions_;
};

}  // namespace comm
}  // namespace nook
