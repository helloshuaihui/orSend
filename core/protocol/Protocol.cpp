#include "Protocol.h"

#include <cstring>

namespace orsend {
namespace proto {

namespace {
inline void WriteU16(std::string& out, uint16_t v) {
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
    out.push_back(static_cast<char>(v & 0xFF));
}
inline void WriteU32(std::string& out, uint32_t v) {
    out.push_back(static_cast<char>((v >> 24) & 0xFF));
    out.push_back(static_cast<char>((v >> 16) & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
    out.push_back(static_cast<char>(v & 0xFF));
}
inline uint16_t ReadU16(const char* p) {
    return static_cast<uint16_t>((static_cast<uint8_t>(p[0]) << 8) | static_cast<uint8_t>(p[1]));
}
inline uint32_t ReadU32(const char* p) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(p[0])) << 24) |
           (static_cast<uint32_t>(static_cast<uint8_t>(p[1])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(p[2])) << 8) |
           static_cast<uint32_t>(static_cast<uint8_t>(p[3]));
}
}  // namespace

const char* MsgTypeName(MsgType type) {
    switch (type) {
        case MsgType::Hello: return "HELLO";
        case MsgType::HelloAck: return "HELLO_ACK";
        case MsgType::Register: return "REGISTER";
        case MsgType::RegisterAck: return "REGISTER_ACK";
        case MsgType::Unregister: return "UNREGISTER";
        case MsgType::TunnelOpen: return "TUNNEL_OPEN";
        case MsgType::TunnelOpenAck: return "TUNNEL_OPEN_ACK";
        case MsgType::Data: return "DATA";
        case MsgType::Close: return "CLOSE";
        case MsgType::Ping: return "PING";
        case MsgType::Pong: return "PONG";
        default: return "UNKNOWN";
    }
}

std::string Encode(uint8_t type, uint32_t streamId, const std::string& payload, uint16_t flags) {
    std::string out;
    out.reserve(kHeaderSize + payload.size());
    WriteU16(out, kMagic);
    out.push_back(static_cast<char>(kVersion));
    out.push_back(static_cast<char>(type));
    WriteU16(out, flags);
    WriteU32(out, streamId);
    WriteU32(out, static_cast<uint32_t>(payload.size()));
    out.append(payload);
    return out;
}

bool Decoder::Feed(const char* data, size_t len) {
    if (error_ || len == 0) return !error_;
    // 定期压缩已消费的前缀，避免缓冲无限增长
    if (readPos_ > 0 && readPos_ >= buf_.size() / 2) {
        buf_.erase(0, readPos_);
        readPos_ = 0;
    }
    buf_.append(data, len);
    return true;
}

void Decoder::Reset() {
    buf_.clear();
    readPos_ = 0;
    error_ = false;
    headerReady_ = false;
}

bool Decoder::ParseHeader() {
    if (buf_.size() - readPos_ < kHeaderSize) return false;
    const char* p = buf_.data() + readPos_;
    const uint16_t magic = ReadU16(p);
    if (magic != kMagic) {
        error_ = true;
        return false;
    }
    const uint8_t ver = static_cast<uint8_t>(p[2]);
    if (ver != kVersion) {
        error_ = true;
        return false;
    }
    type_ = static_cast<uint8_t>(p[3]);
    flags_ = ReadU16(p + 4);
    streamId_ = ReadU32(p + 6);
    payloadLen_ = ReadU32(p + 10);
    if (payloadLen_ > kMaxPayload) {
        error_ = true;
        return false;
    }
    readPos_ += kHeaderSize;
    headerReady_ = true;
    return true;
}

bool Decoder::Next(Frame& out) {
    if (error_) return false;
    if (!headerReady_ && !ParseHeader()) return false;
    if (Buffered() < payloadLen_) return false;

    out.type = type_;
    out.flags = flags_;
    out.streamId = streamId_;
    out.payload.assign(buf_.data() + readPos_, payloadLen_);
    readPos_ += payloadLen_;
    headerReady_ = false;

    if (readPos_ >= buf_.size()) {
        buf_.clear();
        readPos_ = 0;
    }
    return true;
}

}  // namespace proto
}  // namespace orsend
