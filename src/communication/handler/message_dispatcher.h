#pragma once

#include "../protocol/frame.h"

#include <functional>
#include <mutex>
#include <unordered_map>

namespace nook {
namespace comm {

class Session;

class MessageDispatcher {
public:
    using Handler = std::function<void(Session&, const Frame&)>;

    void RegisterHandler(MessageType type, Handler handler);
    bool Dispatch(Session& session, const Frame& frame) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<uint16_t, Handler> handlers_;
};

}  // namespace comm
}  // namespace nook
