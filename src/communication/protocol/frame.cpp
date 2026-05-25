#include "frame.h"

#include <cstring>

namespace nook {
namespace comm {
namespace {

uint16_t ReadUint16BE(const uint8_t* data) {
    return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) |
                                 static_cast<uint16_t>(data[1]));
}

uint32_t ReadUint32BE(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           static_cast<uint32_t>(data[3]);
}

void WriteUint16BE(uint8_t* out, uint16_t value) {
    out[0] = static_cast<uint8_t>((value >> 8) & 0xFF);
    out[1] = static_cast<uint8_t>(value & 0xFF);
}

void WriteUint32BE(uint8_t* out, uint32_t value) {
    out[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
    out[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    out[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    out[3] = static_cast<uint8_t>(value & 0xFF);
}

}  // namespace

Frame::Frame(MessageType type, uint32_t msg_id, std::vector<uint8_t> payload)
    : type_(type), msg_id_(msg_id), payload_(std::move(payload)) {}

std::vector<uint8_t> Frame::Serialize() const {
    std::vector<uint8_t> bytes(kHeaderSize + payload_.size());
    WriteUint32BE(bytes.data(), static_cast<uint32_t>(payload_.size()));
    WriteUint16BE(bytes.data() + 4, static_cast<uint16_t>(type_));
    WriteUint32BE(bytes.data() + 6, msg_id_);
    if (!payload_.empty()) {
        std::memcpy(bytes.data() + kHeaderSize, payload_.data(), payload_.size());
    }
    return bytes;
}

bool Frame::Parse(const uint8_t* data, size_t len, Frame* out, size_t* consumed) {
    if (data == nullptr || out == nullptr || consumed == nullptr || len < kHeaderSize) {
        return false;
    }

    const uint32_t payload_len = ReadUint32BE(data);
    if (payload_len > kMaxPayloadSize) {
        *consumed = 0;
        return false;
    }
    const size_t total_len = kHeaderSize + static_cast<size_t>(payload_len);
    if (len < total_len) {
        return false;
    }

    out->type_ = static_cast<MessageType>(ReadUint16BE(data + 4));
    out->msg_id_ = ReadUint32BE(data + 6);
    out->payload_.assign(data + kHeaderSize, data + total_len);
    *consumed = total_len;
    return true;
}

bool Frame::NeedMoreData(const uint8_t* data, size_t len, size_t* needed) {
    if (needed == nullptr) {
        return false;
    }

    if (data == nullptr) {
        *needed = kHeaderSize;
        return true;
    }

    if (len < kHeaderSize) {
        *needed = kHeaderSize;
        return true;
    }

    const uint32_t payload_len = ReadUint32BE(data);
    if (payload_len > kMaxPayloadSize) {
        *needed = 0;
        return false;
    }
    const size_t total_len = kHeaderSize + static_cast<size_t>(payload_len);
    if (len < total_len) {
        *needed = total_len;
        return true;
    }

    *needed = total_len;
    return false;
}

}  // namespace comm
}  // namespace nook
