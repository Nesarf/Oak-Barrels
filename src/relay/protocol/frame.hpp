// Frame codec for the relay protocol.
//
// Wire form, per docs/RELAY_PROTOCOL.md section 3:
//
//   [u32 length, little-endian][u8 type][payload bytes]
//
// `length` counts the type byte plus the payload, so its minimum is 1. The
// payload is UTF-8 JSON text, except for messages that carry none, which are
// encoded with an empty payload rather than an empty object.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "relay/protocol/messages.hpp"

namespace oak::relay::protocol {

/// A decoded frame.
struct Frame {
  MessageType type = MessageType::Bye;
  std::string payload;  // JSON text; empty when the message carries none
};

/// Encodes one frame, including its length prefix.
///
/// The caller is responsible for keeping the payload within
/// `kMaxControlFrameBytes`; a frame that exceeds the budget is one the peer
/// will reject, and that rejection is the intended outcome.
std::vector<std::uint8_t> encodeFrame(MessageType type, std::string_view payloadJson = {});

/// Why a decoder stopped accepting input.
enum class DecodeError {
  None,
  /// A frame announced more than `kMaxControlFrameBytes`.
  FrameTooLarge,
  /// A frame announced a length below the minimum of 1.
  LengthTooSmall,
  /// The type byte is not defined by this revision.
  UnknownMessageType,
};

/// Human-readable form of [error], for logs and test failures.
const char* decodeErrorSummary(DecodeError error);

/// Incremental frame decoder.
///
/// Feed it bytes as they arrive, then call `next()` until it returns nullopt.
/// A protocol violation is sticky: once `failed()` is true, every later call
/// to `next()` returns nullopt and further input is ignored. Callers must
/// therefore check `failed()` rather than treating a nullopt as "need more
/// data" when a frame was expected.
class FrameDecoder {
 public:
  FrameDecoder() = default;

  void feed(const std::uint8_t* data, std::size_t length);
  void feed(const std::vector<std::uint8_t>& data) { feed(data.data(), data.size()); }

  /// Returns the next complete frame, or nullopt when more bytes are needed
  /// (or after a failure).
  std::optional<Frame> next();

  DecodeError error() const { return error_; }
  bool failed() const { return error_ != DecodeError::None; }

  /// Bytes held but not yet consumed as a frame.
  std::size_t bufferedBytes() const { return buffer_.size(); }

 private:
  void fail(DecodeError error);

  std::vector<std::uint8_t> buffer_;
  DecodeError error_ = DecodeError::None;
};

}  // namespace oak::relay::protocol
