#pragma once

#include "message_types.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nook {
namespace comm {

class Frame {
public:
    static constexpr size_t kHeaderSize = 10;
    static constexpr uint32_t kMaxPayloadSize = 16u * 1024u * 1024u;

    Frame() = default;
    Frame(MessageType type, uint32_t msg_id, std::vector<uint8_t> payload);

    MessageType GetType() const { return type_; }
    uint32_t GetMsgId() const { return msg_id_; }
    const std::vector<uint8_t>& GetPayload() const { return payload_; }

    std::vector<uint8_t> Serialize() const;

    static bool Parse(const uint8_t* data, size_t len, Frame* out, size_t* consumed);
    static bool NeedMoreData(const uint8_t* data, size_t len, size_t* needed);

private:
    MessageType type_ = MessageType::kError;
    uint32_t msg_id_ = 0;
    std::vector<uint8_t> payload_;
};

}  // namespace comm
}  // namespace nook
