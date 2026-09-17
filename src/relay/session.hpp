// The session state machine: one host, one relay, one negotiated session.
//
// The session owns protocol ordering and nothing else. It does not know how
// an engine was found, whether one exists, or what it is called -- that lives
// behind `EngineBackend`. Keeping the split there is what makes it possible
// to test every protocol rule without an engine present.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "relay/backend.hpp"
#include "relay/protocol/frame.hpp"
#include "relay/protocol/json.hpp"
#include "relay/protocol/messages.hpp"

namespace oak::relay {

/// Where a session writes its outgoing frames.
class FrameSink {
 public:
  virtual ~FrameSink() = default;
  virtual bool writeFrame(const std::vector<std::uint8_t>& bytes) = 0;
};

/// A sink that keeps frames in memory. Used by tests.
///
/// Decoding goes back through the real `FrameDecoder`, so a test asserting on
/// `frames()` is also asserting that the encoder produces something the
/// decoder accepts -- a round trip rather than a restatement of the input.
class MemorySink final : public FrameSink {
 public:
  bool writeFrame(const std::vector<std::uint8_t>& bytes) override;

  /// Everything written so far, decoded.
  std::vector<protocol::Frame> frames() const;

  const std::vector<std::vector<std::uint8_t>>& raw() const { return written_; }
  void clear() { written_.clear(); }

 private:
  std::vector<std::vector<std::uint8_t>> written_;
};

/// Drives one session's protocol state machine.
class Session {
 public:
  Session(EngineBackend& backend, FrameSink& sink);

  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  /// Handles one decoded frame, emitting any replies.
  ///
  /// Returns false once the session is over -- after a clean `BYE`, or after a
  /// fatal negotiation failure. A false return does not by itself mean
  /// something went wrong; check the frames written to tell the two apart.
  bool handle(const protocol::Frame& frame);

  bool finished() const { return finished_; }

  /// The agreed revision, or 0 before negotiation completes.
  int revision() const { return revision_; }

  bool diagnosticsEnabled() const { return diagnosticsEnabled_; }
  bool identifyEnabled() const { return identifyEnabled_; }

 private:
  bool handleHello(const protocol::Frame& frame);
  bool handleCapsRequest();
  bool handleOpen(const protocol::json::Value& payload);
  bool handlePost(const protocol::json::Value& payload);
  bool handleSet(const protocol::json::Value& payload);
  bool handleClose(const protocol::json::Value& payload);
  bool handleDiagRequest(const protocol::json::Value& payload);

  void send(protocol::MessageType type, const std::string& payloadJson);
  void sendError(protocol::Reason reason);
  void sendStatus(std::uint32_t handle, protocol::Reason reason);

  EngineBackend& backend_;
  FrameSink& sink_;

  bool finished_ = false;
  bool negotiated_ = false;
  bool diagnosticsEnabled_ = false;
  bool identifyEnabled_ = false;
  int revision_ = 0;
};

}  // namespace oak::relay
