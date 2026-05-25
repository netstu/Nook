#include "tlv.h"

#include <cassert>
#include <cstring>

namespace nook {
namespace comm {
namespace {

uint8_t MakeTag(uint8_t field_id, TlvValueType type) {
    // The compact TLV tag layout only reserves 4 bits for field_id.
    assert(field_id <= 15 && "field_id must be in range [0, 15]");
    return static_cast<uint8_t>((field_id << 4) | (static_cast<uint8_t>(type) & 0x0F));
}

uint8_t ReadFieldId(uint8_t tag) {
    return static_cast<uint8_t>((tag >> 4) & 0x0F);
}

TlvValueType ReadType(uint8_t tag) {
    return static_cast<TlvValueType>(tag & 0x0F);
}

void AppendUint16BE(std::vector<uint8_t>* out, uint16_t value) {
    out->push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out->push_back(static_cast<uint8_t>(value & 0xFF));
}

void AppendUint32BE(std::vector<uint8_t>* out, uint32_t value) {
    out->push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out->push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out->push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out->push_back(static_cast<uint8_t>(value & 0xFF));
}

void AppendUint64BE(std::vector<uint8_t>* out, uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        out->push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
    }
}

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

uint64_t ReadUint64BE(const uint8_t* data) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | static_cast<uint64_t>(data[i]);
    }
    return value;
}

}  // namespace

void TlvEncoder::PutField(uint8_t field_id, TlvValueType type, const uint8_t* data, size_t len) {
    bytes_.push_back(MakeTag(field_id, type));
    AppendUint16BE(&bytes_, static_cast<uint16_t>(len));
    if (len > 0 && data != nullptr) {
        bytes_.insert(bytes_.end(), data, data + len);
    }
}

void TlvEncoder::PutUint8(uint8_t field_id, uint8_t value) {
    PutField(field_id, TlvValueType::kUint8, &value, sizeof(value));
}

void TlvEncoder::PutUint16(uint8_t field_id, uint16_t value) {
    uint8_t buf[2];
    buf[0] = static_cast<uint8_t>((value >> 8) & 0xFF);
    buf[1] = static_cast<uint8_t>(value & 0xFF);
    PutField(field_id, TlvValueType::kUint16, buf, sizeof(buf));
}

void TlvEncoder::PutUint32(uint8_t field_id, uint32_t value) {
    uint8_t buf[4];
    buf[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
    buf[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    buf[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    buf[3] = static_cast<uint8_t>(value & 0xFF);
    PutField(field_id, TlvValueType::kUint32, buf, sizeof(buf));
}

void TlvEncoder::PutUint64(uint8_t field_id, uint64_t value) {
    uint8_t buf[8];
    for (int i = 0; i < 8; ++i) {
        const int shift = 56 - (i * 8);
        buf[i] = static_cast<uint8_t>((value >> shift) & 0xFF);
    }
    PutField(field_id, TlvValueType::kUint64, buf, sizeof(buf));
}

void TlvEncoder::PutString(uint8_t field_id, const std::string& value) {
    PutField(field_id,
             TlvValueType::kString,
             reinterpret_cast<const uint8_t*>(value.data()),
             value.size());
}

void TlvEncoder::PutBytes(uint8_t field_id, const uint8_t* data, size_t len) {
    PutField(field_id, TlvValueType::kBytes, data, len);
}

void TlvEncoder::PutNested(uint8_t field_id, const TlvEncoder& nested) {
    const std::vector<uint8_t> nested_bytes = nested.Build();
    PutField(field_id, TlvValueType::kNested, nested_bytes.data(), nested_bytes.size());
}

TlvDecoder::TlvDecoder(const uint8_t* data, size_t len) {
    if (data != nullptr && len > 0) {
        bytes_.assign(data, data + len);
    }
}

bool TlvDecoder::FindField(uint8_t field_id,
                           TlvValueType expected_type,
                           const uint8_t** value,
                           size_t* value_len) const {
    if (value == nullptr || value_len == nullptr) {
        return false;
    }

    size_t offset = 0;
    while (offset + 3 <= bytes_.size()) {
        const uint8_t tag = bytes_[offset];
        const uint8_t current_field_id = ReadFieldId(tag);
        const TlvValueType current_type = ReadType(tag);
        const uint16_t len = ReadUint16BE(bytes_.data() + offset + 1);
        offset += 3;
        if (offset + len > bytes_.size()) {
            return false;
        }

        if (current_field_id == field_id && current_type == expected_type) {
            *value = bytes_.data() + offset;
            *value_len = len;
            return true;
        }
        offset += len;
    }
    return false;
}

bool TlvDecoder::GetUint8(uint8_t field_id, uint8_t* out) const {
    const uint8_t* value = nullptr;
    size_t len = 0;
    if (!out || !FindField(field_id, TlvValueType::kUint8, &value, &len) || len != 1) {
        return false;
    }
    *out = value[0];
    return true;
}

bool TlvDecoder::GetUint16(uint8_t field_id, uint16_t* out) const {
    const uint8_t* value = nullptr;
    size_t len = 0;
    if (!out || !FindField(field_id, TlvValueType::kUint16, &value, &len) || len != 2) {
        return false;
    }
    *out = ReadUint16BE(value);
    return true;
}

bool TlvDecoder::GetUint32(uint8_t field_id, uint32_t* out) const {
    const uint8_t* value = nullptr;
    size_t len = 0;
    if (!out || !FindField(field_id, TlvValueType::kUint32, &value, &len) || len != 4) {
        return false;
    }
    *out = ReadUint32BE(value);
    return true;
}

bool TlvDecoder::GetUint64(uint8_t field_id, uint64_t* out) const {
    const uint8_t* value = nullptr;
    size_t len = 0;
    if (!out || !FindField(field_id, TlvValueType::kUint64, &value, &len) || len != 8) {
        return false;
    }
    *out = ReadUint64BE(value);
    return true;
}

bool TlvDecoder::GetString(uint8_t field_id, std::string* out) const {
    const uint8_t* value = nullptr;
    size_t len = 0;
    if (!out || !FindField(field_id, TlvValueType::kString, &value, &len)) {
        return false;
    }
    out->assign(reinterpret_cast<const char*>(value), len);
    return true;
}

bool TlvDecoder::GetBytes(uint8_t field_id, std::vector<uint8_t>* out) const {
    const uint8_t* value = nullptr;
    size_t len = 0;
    if (!out || !FindField(field_id, TlvValueType::kBytes, &value, &len)) {
        return false;
    }
    out->assign(value, value + len);
    return true;
}

bool TlvDecoder::GetNested(uint8_t field_id, TlvDecoder* out) const {
    const uint8_t* value = nullptr;
    size_t len = 0;
    if (!out || !FindField(field_id, TlvValueType::kNested, &value, &len)) {
        return false;
    }
    *out = TlvDecoder(value, len);
    return true;
}

bool TlvDecoder::GetAllNested(uint8_t field_id, std::vector<TlvDecoder>* out) const {
    if (out == nullptr) {
        return false;
    }

    out->clear();
    size_t offset = 0;
    while (offset + 3 <= bytes_.size()) {
        const uint8_t tag = bytes_[offset];
        const uint8_t current_field_id = ReadFieldId(tag);
        const TlvValueType current_type = ReadType(tag);
        const uint16_t len = ReadUint16BE(bytes_.data() + offset + 1);
        offset += 3;
        if (offset + len > bytes_.size()) {
            out->clear();
            return false;
        }

        if (current_field_id == field_id && current_type == TlvValueType::kNested) {
            out->emplace_back(bytes_.data() + offset, len);
        }
        offset += len;
    }
    return true;
}

}  // namespace comm
}  // namespace nook
