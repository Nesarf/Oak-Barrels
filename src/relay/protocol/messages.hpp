// Message types and reason codes for the relay protocol.
//
// Declarations only; see docs/RELAY_PROTOCOL.md section 3.1 and section 6, which are
// normative. Numeric values here must match the Dart side exactly.
#pragma once

#include <cstdint>

namespace oak::relay::protocol {

/// The protocol revision this implementation speaks.
inline constexpr int kRevision = 1;

/// Ceiling on a single control frame, per protocol section 3.
inline constexpr std::uint32_t kMaxControlFrameBytes = 1024u * 1024u;

/// Message type byte, per protocol section 3.1.
enum class MessageType : std::uint8_t {
  Hello = 0x01,
  HelloAck = 0x02,
  CapsRequest = 0x10,
  CapsReply = 0x11,
  Open = 0x20,
  OpenAck = 0x21,
  Post = 0x30,
  Set = 0x31,
  Close = 0x40,
  Status = 0x50,
  DiagRequest = 0x60,
  DiagReply = 0x61,
  Bye = 0x70,
  Error = 0x7f,
};

/// Returns true when [raw] is a type this revision defines.
inline bool isKnownMessageType(std::uint8_t raw) {
  switch (static_cast<MessageType>(raw)) {
    case MessageType::Hello:
    case MessageType::HelloAck:
    case MessageType::CapsRequest:
    case MessageType::CapsReply:
    case MessageType::Open:
    case MessageType::OpenAck:
    case MessageType::Post:
    case MessageType::Set:
    case MessageType::Close:
    case MessageType::Status:
    case MessageType::DiagRequest:
    case MessageType::DiagReply:
    case MessageType::Bye:
    case MessageType::Error:
      return true;
  }
  return false;
}

/// Failure reason codes, per protocol section 6.
///
/// A reason code is the only thing the relay says about a failure. It must
/// never be accompanied by text naming a vendor, a version, or a path -- that
/// constraint is what keeps diagnostics from becoming a machine fingerprint.
enum class Reason : std::uint32_t {
  Ok = 0,
  NoEngine = 1,
  NoCommonRevision = 2,
  IncompatibleEngine = 3,
  BindFailed = 4,
  UnsupportedOperation = 5,
  InvalidHandle = 6,
  BadArgument = 7,
  Busy = 8,
  Internal = 9,
  DiagDisabled = 10,
};

/// A generic, non-identifying description for [reason].
///
/// Returns a string literal; these must stay free of anything that could
/// identify the machine, its software, or its layout.
inline const char* reasonSummary(Reason reason) {
  switch (reason) {
    case Reason::Ok:
      return "ok";
    case Reason::NoEngine:
      return "no candidate installation found";
    case Reason::NoCommonRevision:
      return "protocol revisions do not overlap";
    case Reason::IncompatibleEngine:
      return "no known compatibility class fits";
    case Reason::BindFailed:
      return "dynamic symbol resolution failed";
    case Reason::UnsupportedOperation:
      return "capability not present for this engine";
    case Reason::InvalidHandle:
      return "unknown or closed target";
    case Reason::BadArgument:
      return "malformed or out-of-range argument";
    case Reason::Busy:
      return "another session holds the relay";
    case Reason::Internal:
      return "internal error";
    case Reason::DiagDisabled:
      return "diagnostics requested without opt-in";
  }
  return "unmapped";
}

}  // namespace oak::relay::protocol
