#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nook {
namespace comm {

enum class TlvValueType : uint8_t {
    kUint8  = 0x1,
    kUint16 = 0x2,
    kUint32 = 0x3,
    kUint64 = 0x4,
    kString = 0x5,
    kBytes  = 0x6,
    kNested = 0x7,
};

class TlvEncoder {
public:
    void PutUint8(uint8_t field_id, uint8_t value);
    void PutUint16(uint8_t field_id, uint16_t value);
    void PutUint32(uint8_t field_id, uint32_t value);
    void PutUint64(uint8_t field_id, uint64_t value);
    void PutString(uint8_t field_id, const std::string& value);
    void PutBytes(uint8_t field_id, const uint8_t* data, size_t len);
    void PutNested(uint8_t field_id, const TlvEncoder& nested);

    std::vector<uint8_t> Build() const { return bytes_; }

private:
    void PutField(uint8_t field_id, TlvValueType type, const uint8_t* data, size_t len);

    std::vector<uint8_t> bytes_;
};

class TlvDecoder {
public:
    TlvDecoder() = default;
    TlvDecoder(const uint8_t* data, size_t len);

    bool GetUint8(uint8_t field_id, uint8_t* out) const;
    bool GetUint16(uint8_t field_id, uint16_t* out) const;
    bool GetUint32(uint8_t field_id, uint32_t* out) const;
    bool GetUint64(uint8_t field_id, uint64_t* out) const;
    bool GetString(uint8_t field_id, std::string* out) const;
    bool GetBytes(uint8_t field_id, std::vector<uint8_t>* out) const;
    bool GetNested(uint8_t field_id, TlvDecoder* out) const;
    bool GetAllNested(uint8_t field_id, std::vector<TlvDecoder>* out) const;

private:
    bool FindField(uint8_t field_id,
                   TlvValueType expected_type,
                   const uint8_t** value,
                   size_t* value_len) const;

    std::vector<uint8_t> bytes_;
};

}  // namespace comm
}  // namespace nook
