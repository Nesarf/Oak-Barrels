#include "relay/session.hpp"

#include <utility>

namespace oak::relay {
namespace {

using protocol::json::Value;
using protocol::MessageType;
using protocol::Reason;

/// Protocol revisions this station implements, newest first.
///
/// A build-time list of *our own* revisions is not the thing axiom A2 forbids.
/// A2 forbids baking in facts about the engine on the other end -- its version,
/// its layout, its symbol names. Nothing here says anything about an engine.
const std::vector<int>& supportedRevisions() {
  static const std::vector<int> revisions{1};
  return revisions;
}

/// Highest revision present in both lists, or 0 when they do not overlap (section 4.1).
int selectRevision(const std::vector<int>& hostRevisions) {
  int best = 0;
  for (const int candidate : hostRevisions) {
    for (const int supported : supportedRevisions()) {
      if (candidate == supported && candidate > best) best = candidate;
    }
  }
  return best;
}

/// Parses a frame payload. An absent payload reads as an empty object, which
/// is what several messages legitimately carry.
std::optional<Value> parsePayload(const protocol::Frame& frame) {
  if (frame.payload.empty()) return Value::object();
  return protocol::json::parse(frame.payload);
}

/// Reads a handle field, rejecting zero and negatives (section 5: handles are
/// opaque and non-zero).
std::optional<std::uint32_t> readHandle(const Value& payload) {
  const Value* handle = payload.find("handle");
  if (handle == nullptr || !handle->isNumber()) return std::nullopt;

  const std::int64_t raw = handle->asInt();
  if (raw <= 0 || raw > 0xffffffffLL) return std::nullopt;
  return static_cast<std::uint32_t>(raw);
}

}  // namespace

// ------------------------------------------------------------------ MemorySink

bool MemorySink::writeFrame(const std::vector<std::uint8_t>& bytes) {
  written_.push_back(bytes);
  return true;
}

std::vector<protocol::Frame> MemorySink::frames() const {
  protocol::FrameDecoder decoder;
  std::vector<protocol::Frame> decoded;

  for (const auto& chunk : written_) {
    decoder.feed(chunk);
    while (auto frame = decoder.next()) {
      decoded.push_back(std::move(*frame));
    }
  }

  return decoded;
}

// --------------------------------------------------------------------- Session

Session::Session(EngineBackend& backend, FrameSink& sink)
    : backend_(backend), sink_(sink) {}

void Session::send(MessageType type, const std::string& payloadJson) {
  (void)sink_.writeFrame(protocol::encodeFrame(type, payloadJson));
}

void Session::sendError(Reason reason) {
  // section 6: a reason code plus a short, non-identifying phrase. The phrase comes
  // from a fixed table, so it can never leak what was actually found.
  Value payload = Value::object();
  payload.set("reason", Value::integer(static_cast<std::int64_t>(reason)));
  payload.set("detail", Value::string(protocol::reasonSummary(reason)));
  send(MessageType::Error, payload.dump());
}

void Session::sendStatus(std::uint32_t handle, Reason reason) {
  Value payload = Value::object();
  payload.set("handle", Value::integer(static_cast<std::int64_t>(handle)));
  payload.set("reason", Value::integer(static_cast<std::int64_t>(reason)));
  send(MessageType::Status, payload.dump());
}

bool Session::handle(const protocol::Frame& frame) {
  if (finished_) return false;

  // BYE is acceptable at any point, including mid-negotiation.
  if (frame.type == MessageType::Bye) {
    send(MessageType::Bye, {});
    finished_ = true;
    return false;
  }

  if (!negotiated_) {
    if (frame.type != MessageType::Hello) {
      // A peer that speaks before HELLO is not speaking this protocol.
      // Following the pattern of section 4.1 -- report, then close.
      sendError(Reason::BadArgument);
      finished_ = true;
      return false;
    }
    return handleHello(frame);
  }

  std::optional<Value> payload = parsePayload(frame);
  if (!payload.has_value()) {
    sendError(Reason::BadArgument);
    return true;
  }

  switch (frame.type) {
    case MessageType::CapsRequest:
      return handleCapsRequest();
    case MessageType::Open:
      return handleOpen(*payload);
    case MessageType::Post:
      return handlePost(*payload);
    case MessageType::Set:
      return handleSet(*payload);
    case MessageType::Close:
      return handleClose(*payload);
    case MessageType::DiagRequest:
      return handleDiagRequest(*payload);
    default:
      break;
  }

  // A second HELLO, or a reply-direction message, inside a live session.
  sendError(Reason::UnsupportedOperation);
  return true;
}

bool Session::handleHello(const protocol::Frame& frame) {
  const std::optional<Value> payload = parsePayload(frame);
  if (!payload.has_value()) {
    sendError(Reason::BadArgument);
    finished_ = true;
    return false;
  }

  const Value* revisions = payload->find("revisions");
  if (revisions == nullptr || !revisions->isArray()) {
    sendError(Reason::BadArgument);
    finished_ = true;
    return false;
  }

  std::vector<int> offered;
  offered.reserve(revisions->asArray().size());
  for (const Value& entry : revisions->asArray()) {
    if (entry.isNumber()) offered.push_back(static_cast<int>(entry.asInt()));
  }

  // Feature strings are read and discarded: section 4.2 makes them advisory, and an
  // unknown one must never be fatal.
  const int selected = selectRevision(offered);
  if (selected == 0) {
    sendError(Reason::NoCommonRevision);
    finished_ = true;
    return false;
  }

  revision_ = selected;
  negotiated_ = true;

  Value ack = Value::object();
  ack.set("revision", Value::integer(selected));
  ack.set("features", Value::array());
  send(MessageType::HelloAck, ack.dump());
  return true;
}

bool Session::handleCapsRequest() {
  std::vector<Value> classes;
  for (const std::string& name : backend_.capabilityClasses()) {
    classes.push_back(Value::string(name));
  }

  Value reply = Value::object();
  reply.set("classes", Value::array(std::move(classes)));
  reply.set("limits", backend_.limits());
  reply.set("bulk", Value::boolean(backend_.bulkChannel()));
  send(MessageType::CapsReply, reply.dump());
  return true;
}

bool Session::handleOpen(const Value& payload) {
  const Value* kind = payload.find("kind");
  if (kind == nullptr || !kind->isString()) {
    sendError(Reason::BadArgument);
    return true;
  }

  std::string name;
  if (const Value* given = payload.find("name"); given != nullptr && given->isString()) {
    name = given->asString();
  }

  Reason reason = Reason::Ok;
  const std::uint32_t handle = backend_.openTarget(kind->asString(), name, reason);
  if (handle == 0) {
    sendError(reason == Reason::Ok ? Reason::Internal : reason);
    return true;
  }

  Value ack = Value::object();
  ack.set("handle", Value::integer(static_cast<std::int64_t>(handle)));
  send(MessageType::OpenAck, ack.dump());
  return true;
}

bool Session::handlePost(const Value& payload) {
  const std::optional<std::uint32_t> handle = readHandle(payload);
  if (!handle.has_value()) {
    sendError(Reason::BadArgument);
    return true;
  }

  const Value* action = payload.find("action");
  if (action == nullptr || !action->isString()) {
    sendError(Reason::BadArgument);
    return true;
  }

  Value args = Value::object();
  if (const Value* given = payload.find("args"); given != nullptr && given->isObject()) {
    args = *given;
  }

  const Reason reason = backend_.post(*handle, action->asString(), args);
  if (reason == Reason::Ok) {
    sendStatus(*handle, reason);
  } else {
    // An operation failure must travel as ERROR: a host treats any non-error
    // reply as success, so a failure sent as STATUS would be silently dropped.
    sendError(reason);
  }
  return true;
}

bool Session::handleSet(const Value& payload) {
  const std::optional<std::uint32_t> handle = readHandle(payload);
  if (!handle.has_value()) {
    sendError(Reason::BadArgument);
    return true;
  }

  const Value* param = payload.find("param");
  if (param == nullptr || !param->isString()) {
    sendError(Reason::BadArgument);
    return true;
  }

  const Value* value = payload.find("value");
  if (value == nullptr || !value->isNumber()) {
    sendError(Reason::BadArgument);
    return true;
  }

  const double raw = value->asNumber();
  // section 5: value is in [0, 1] unless a negotiated feature says otherwise. The
  // negated form also rejects NaN, which compares false against both bounds.
  if (!(raw >= 0.0 && raw <= 1.0)) {
    sendError(Reason::BadArgument);
    return true;
  }

  const Reason reason = backend_.set(*handle, param->asString(), raw);
  if (reason == Reason::Ok) {
    sendStatus(*handle, reason);
  } else {
    sendError(reason);
  }
  return true;
}

bool Session::handleClose(const Value& payload) {
  const std::optional<std::uint32_t> handle = readHandle(payload);
  if (!handle.has_value()) {
    sendError(Reason::BadArgument);
    return true;
  }

  // Idempotent by contract, so the backend reports Ok for an unknown handle
  // and no INVALID_HANDLE path is needed here.
  const Reason reason = backend_.closeTarget(*handle);
  if (reason == Reason::Ok) {
    sendStatus(*handle, reason);
  } else {
    sendError(reason);
  }
  return true;
}

bool Session::handleDiagRequest(const Value& payload) {
  const Value* enable = payload.find("enable");
  const Value* identify = payload.find("identify");

  // section 8: two independent switches. `diagnose` gates diagnostics at all;
  // `identify` additionally permits names, and is off by default.
  if (enable != nullptr && enable->isBool()) {
    diagnosticsEnabled_ = enable->asBool();
    if (!diagnosticsEnabled_) identifyEnabled_ = false;  // identifying without diagnosing is meaningless
  }

  if (identify != nullptr && identify->isBool() && identify->asBool()) {
    if (!diagnosticsEnabled_) {
      sendError(Reason::DiagDisabled);
      return true;
    }
    identifyEnabled_ = true;
  }

  const bool querying = enable == nullptr && identify == nullptr;
  if (querying && !diagnosticsEnabled_) {
    sendError(Reason::DiagDisabled);
    return true;
  }

  Value reply = Value::object();
  reply.set("diagnose", Value::boolean(diagnosticsEnabled_));
  reply.set("identify", Value::boolean(identifyEnabled_));
  reply.set("revision", Value::integer(revision_));
  reply.set("targets", Value::integer(static_cast<std::int64_t>(backend_.targetCount())));
  reply.set("symbols_resolved",
            Value::integer(static_cast<std::int64_t>(backend_.resolvedSymbolCount())));
  reply.set("files_examined",
            Value::integer(static_cast<std::int64_t>(backend_.filesExamined())));
  reply.set("candidates_found",
            Value::integer(static_cast<std::int64_t>(backend_.candidatesFound())));
  reply.set("discovery_truncated", Value::boolean(backend_.discoveryTruncated()));
  reply.set("bulk", Value::boolean(backend_.bulkChannel()));
  reply.set("bulk_bytes_forwarded",
            Value::integer(static_cast<std::int64_t>(backend_.bulkBytesForwarded())));
  reply.set("bulk_bytes_dropped",
            Value::integer(static_cast<std::int64_t>(backend_.bulkBytesDropped())));

  std::vector<Value> classes;
  for (const std::string& name : backend_.capabilityClasses()) {
    classes.push_back(Value::string(name));
  }
  reply.set("classes", Value::array(std::move(classes)));

  // Deliberately absent: paths, version strings, installation identifiers and
  // machine or user identifiers. A redacted reply is built by never collecting
  // those facts, not by stripping them at the end -- stripping is a step that
  // can be forgotten, omission cannot.

  send(MessageType::DiagReply, reply.dump());
  return true;
}

}  // namespace oak::relay
