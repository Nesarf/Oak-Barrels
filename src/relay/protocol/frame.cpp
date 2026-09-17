#include "relay/protocol/frame.hpp"

namespace oak::relay::protocol {
namespace {

constexpr std::size_t kHeaderBytes = 4;

std::uint32_t readLengthLittleEndian(const std::uint8_t* bytes) {
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8) |
         (static_cast<std::uint32_t>(bytes[2]) << 16) |
         (static_cast<std::uint32_t>(bytes[3]) << 24);
}

}  // namespace

const char* decodeErrorSummary(DecodeError error) {
  switch (error) {
    case DecodeError::None:
      return "no error";
    case DecodeError::FrameTooLarge:
      return "frame exceeds the control budget";
    case DecodeError::LengthTooSmall:
      return "frame length below the minimum of 1";
    case DecodeError::UnknownMessageType:
      return "message type not defined by this revision";
  }
  return "unmapped";
}

std::vector<std::uint8_t> encodeFrame(MessageType type, std::string_view payloadJson) {
  // `length` counts the type byte plus the payload.
  const std::uint32_t bodyLength = 1u + static_cast<std::uint32_t>(payloadJson.size());

  std::vector<std::uint8_t> out;
  out.reserve(kHeaderBytes + bodyLength);
  out.push_back(static_cast<std::uint8_t>(bodyLength & 0xffu));
  out.push_back(static_cast<std::uint8_t>((bodyLength >> 8) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((bodyLength >> 16) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((bodyLength >> 24) & 0xffu));
  out.push_back(static_cast<std::uint8_t>(type));
  out.insert(out.end(), payloadJson.begin(), payloadJson.end());
  return out;
}

void FrameDecoder::fail(DecodeError error) {
  if (error_ == DecodeError::None) error_ = error;
}

void FrameDecoder::feed(const std::uint8_t* data, std::size_t length) {
  if (failed() || data == nullptr || length == 0) return;
  buffer_.insert(buffer_.end(), data, data + length);
}

std::optional<Frame> FrameDecoder::next() {
  if (failed()) return std::nullopt;
  if (buffer_.size() < kHeaderBytes) return std::nullopt;

  const std::uint32_t length = readLengthLittleEndian(buffer_.data());
  if (length < 1) {
    fail(DecodeError::LengthTooSmall);
    return std::nullopt;
  }
  if (length > kMaxControlFrameBytes) {
    fail(DecodeError::FrameTooLarge);
    return std::nullopt;
  }

  const std::size_t total = kHeaderBytes + static_cast<std::size_t>(length);
  if (buffer_.size() < total) return std::nullopt;  // need more bytes

  const std::uint8_t rawType = buffer_[kHeaderBytes];
  if (!isKnownMessageType(rawType)) {
    fail(DecodeError::UnknownMessageType);
    return std::nullopt;
  }

  Frame frame;
  frame.type = static_cast<MessageType>(rawType);
  frame.payload.assign(buffer_.begin() + static_cast<std::ptrdiff_t>(kHeaderBytes + 1),
                       buffer_.begin() + static_cast<std::ptrdiff_t>(total));

  buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(total));
  return frame;
}

}  // namespace oak::relay::protocol
