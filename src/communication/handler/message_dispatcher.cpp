#include "message_dispatcher.h"

#include "../session/session.h"

namespace nook {
namespace comm {

void MessageDispatcher::RegisterHandler(MessageType type, Handler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_[static_cast<uint16_t>(type)] = std::move(handler);
}

bool MessageDispatcher::Dispatch(Session& session, const Frame& frame) const {
    Handler handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = handlers_.find(static_cast<uint16_t>(frame.GetType()));
        if (it == handlers_.end()) {
            return false;
        }
        handler = it->second;
    }

    if (!handler) {
        return false;
    }
    handler(session, frame);
    return true;
}

}  // namespace comm
}  // namespace nook
