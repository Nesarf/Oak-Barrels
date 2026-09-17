#include "harness.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "relay/backend.hpp"
#include "relay/protocol/json.hpp"
#include "relay/session.hpp"

namespace {

using oak::relay::EngineBackend;
using oak::relay::MemorySink;
using oak::relay::NullBackend;
using oak::relay::protocol::Frame;
using oak::relay::protocol::MessageType;
using oak::relay::protocol::Reason;
using oak::relay::protocol::json::parse;
using oak::relay::protocol::json::Value;
using oak::relay::Session;

Frame makeFrame(MessageType type, std::string payload = {}) {
  Frame frame;
  frame.type = type;
  frame.payload = std::move(payload);
  return frame;
}

/// Reads a member of a frame's payload, failing the test if it is absent.
Value field(const Frame& frame, const std::string& key) {
  const auto parsed = parse(frame.payload);
  OAK_CHECK(parsed.has_value());
  const Value* found = parsed->find(key);
  OAK_CHECK(found != nullptr);
  return *found;
}

/// A backend that claims a small, known set of behaviours, so the session's
/// own rules can be tested without an engine present.
class FakeBackend final : public EngineBackend {
 public:
  std::vector<std::string> capabilityClasses() const override {
    return {"engine.action.named", "engine.param.continuous"};
  }

  Value limits() const override {
    Value value = Value::object();
    value.set("max_targets", Value::integer(64));
    value.set("max_param_rate_hz", Value::integer(60));
    return value;
  }

  bool bulkChannel() const override { return false; }

  std::uint32_t openTarget(const std::string& kind,
                           const std::string& /*name*/,
                           Reason& reason) override {
    if (kind != "emitter") {
      reason = Reason::UnsupportedOperation;
      return 0;
    }
    reason = Reason::Ok;
    return nextHandle_++;
  }

  Reason closeTarget(std::uint32_t /*handle*/) override { return Reason::Ok; }

  Reason post(std::uint32_t /*handle*/,
              const std::string& action,
              const Value& /*args*/) override {
    if (action == "unavailable") return Reason::UnsupportedOperation;
    if (action == "boom") return Reason::Internal;
    return Reason::Ok;
  }

  Reason set(std::uint32_t /*handle*/,
             const std::string& /*param*/,
             double /*value*/) override {
    return Reason::Ok;
  }

  std::uint32_t resolvedSymbolCount() const override { return 7; }
  std::uint32_t targetCount() const override { return 2; }

 private:
  std::uint32_t nextHandle_ = 1;
};

/// A session wired to a fake backend and an in-memory sink.
struct Fixture {
  FakeBackend backend;
  MemorySink sink;
  Session session{backend, sink};

  void send(MessageType type, std::string payload = {}) {
    session.handle(makeFrame(type, std::move(payload)));
  }

  std::vector<Frame> frames() const { return sink.frames(); }

  /// Completes the handshake and discards the replies it produced.
  void negotiate() {
    send(MessageType::Hello, R"({"revisions":[1]})");
    sink.clear();
  }
};

}  // namespace

// ---------------------------------------------------------------- negotiation

OAK_TEST(session_negotiates_the_highest_common_revision) {
  Fixture fixture;
  fixture.send(MessageType::Hello, R"({"revisions":[1]})");

  const auto frames = fixture.frames();
  OAK_CHECK_EQ(frames.size(), 1u);
  OAK_CHECK(frames[0].type == MessageType::HelloAck);
  OAK_CHECK_EQ(field(frames[0], "revision").asInt(), 1);
  OAK_CHECK_EQ(fixture.session.revision(), 1);
  OAK_CHECK(!fixture.session.finished());
}

OAK_TEST(session_reports_no_common_revision_and_closes) {
  Fixture fixture;
  fixture.send(MessageType::Hello, R"({"revisions":[9,8]})");

  const auto frames = fixture.frames();
  OAK_CHECK_EQ(frames.size(), 1u);
  OAK_CHECK(frames[0].type == MessageType::Error);
  OAK_CHECK_EQ(field(frames[0], "reason").asInt(), 2);
  OAK_CHECK(fixture.session.finished());
}

OAK_TEST(session_ignores_unknown_features) {
  Fixture fixture;
  // section 4.2: an unrecognised feature string is never fatal, which is what lets
  // one side gain a feature without waiting for the other.
  fixture.send(MessageType::Hello,
               R"({"revisions":[1],"features":["teleport","time-travel"]})");

  const auto frames = fixture.frames();
  OAK_CHECK_EQ(frames.size(), 1u);
  OAK_CHECK(frames[0].type == MessageType::HelloAck);
}

OAK_TEST(session_rejects_a_malformed_hello) {
  {
    Fixture fixture;
    fixture.send(MessageType::Hello, "not json at all");
    const auto frames = fixture.frames();
    OAK_CHECK_EQ(frames.size(), 1u);
    OAK_CHECK(frames[0].type == MessageType::Error);
    OAK_CHECK_EQ(field(frames[0], "reason").asInt(), 7);
    OAK_CHECK(fixture.session.finished());
  }
  {
    Fixture fixture;
    fixture.send(MessageType::Hello, R"({"revisions":"one"})");
    OAK_CHECK(fixture.frames()[0].type == MessageType::Error);
    OAK_CHECK(fixture.session.finished());
  }
  {
    Fixture fixture;
    fixture.send(MessageType::Hello, R"({})");
    OAK_CHECK(fixture.frames()[0].type == MessageType::Error);
    OAK_CHECK(fixture.session.finished());
  }
}

OAK_TEST(session_rejects_a_message_before_hello) {
  Fixture fixture;
  fixture.send(MessageType::CapsRequest, "{}");

  const auto frames = fixture.frames();
  OAK_CHECK_EQ(frames.size(), 1u);
  OAK_CHECK(frames[0].type == MessageType::Error);
  OAK_CHECK_EQ(field(frames[0], "reason").asInt(), 7);
  OAK_CHECK(fixture.session.finished());
}

OAK_TEST(session_honours_bye_before_negotiation) {
  Fixture fixture;
  fixture.send(MessageType::Bye);

  const auto frames = fixture.frames();
  OAK_CHECK_EQ(frames.size(), 1u);
  OAK_CHECK(frames[0].type == MessageType::Bye);
  OAK_CHECK(fixture.session.finished());
}

OAK_TEST(session_answers_bye_with_bye) {
  Fixture fixture;
  fixture.negotiate();
  fixture.send(MessageType::Bye);

  const auto frames = fixture.frames();
  OAK_CHECK_EQ(frames.size(), 1u);
  OAK_CHECK(frames[0].type == MessageType::Bye);
  OAK_CHECK(fixture.session.finished());
}

OAK_TEST(session_ignores_everything_after_finishing) {
  Fixture fixture;
  fixture.negotiate();
  fixture.send(MessageType::Bye);
  const std::size_t afterBye = fixture.frames().size();

  fixture.send(MessageType::CapsRequest, "{}");
  OAK_CHECK_EQ(fixture.frames().size(), afterBye);
}

// --------------------------------------------------------------- capabilities

OAK_TEST(session_reports_capabilities_as_classes_not_versions) {
  Fixture fixture;
  fixture.negotiate();
  fixture.send(MessageType::CapsRequest, "{}");

  const auto frames = fixture.frames();
  OAK_CHECK_EQ(frames.size(), 1u);
  OAK_CHECK(frames[0].type == MessageType::CapsReply);

  const Value classes = field(frames[0], "classes");
  OAK_CHECK(classes.isArray());
  OAK_CHECK_EQ(classes.asArray().size(), 2u);
  OAK_CHECK_EQ(classes.asArray()[0].asString(), std::string("engine.action.named"));
  OAK_CHECK_EQ(classes.asArray()[1].asString(), std::string("engine.param.continuous"));

  const Value limits = field(frames[0], "limits");
  OAK_CHECK(limits.isObject());
  OAK_CHECK_EQ(limits.find("max_targets")->asInt(), 64);

  OAK_CHECK_EQ(field(frames[0], "bulk").asBool(), false);
}

// ------------------------------------------------------------------ operations

OAK_TEST(session_opens_a_target) {
  Fixture fixture;
  fixture.negotiate();
  fixture.send(MessageType::Open, R"({"kind":"emitter","name":"ui"})");

  const auto frames = fixture.frames();
  OAK_CHECK_EQ(frames.size(), 1u);
  OAK_CHECK(frames[0].type == MessageType::OpenAck);
  OAK_CHECK_EQ(field(frames[0], "handle").asInt(), 1);
}

OAK_TEST(session_refuses_an_unsupported_target_kind) {
  Fixture fixture;
  fixture.negotiate();
  fixture.send(MessageType::Open, R"({"kind":"bus","name":"ui"})");

  const auto frames = fixture.frames();
  OAK_CHECK_EQ(frames.size(), 1u);
  OAK_CHECK(frames[0].type == MessageType::Error);
  OAK_CHECK_EQ(field(frames[0], "reason").asInt(), 5);
}

OAK_TEST(session_rejects_an_open_without_a_kind) {
  Fixture fixture;
  fixture.negotiate();
  fixture.send(MessageType::Open, R"({"name":"ui"})");

  OAK_CHECK(fixture.frames()[0].type == MessageType::Error);
  OAK_CHECK_EQ(field(fixture.frames()[0], "reason").asInt(), 7);
}

OAK_TEST(session_acknowledges_a_successful_post) {
  Fixture fixture;
  fixture.negotiate();
  fixture.send(MessageType::Post, R"({"handle":1,"action":"ice_drop","args":{}})");

  const auto frames = fixture.frames();
  OAK_CHECK_EQ(frames.size(), 1u);
  OAK_CHECK(frames[0].type == MessageType::Status);
  OAK_CHECK_EQ(field(frames[0], "reason").asInt(), 0);
}

OAK_TEST(session_reports_a_failed_post_as_an_error) {
  // A host treats any non-error reply as success, so an operation failure
  // travelling as STATUS would be dropped on the floor. This is the test that
  // pins that decision down.
  Fixture fixture;
  fixture.negotiate();

  fixture.send(MessageType::Post, R"({"handle":1,"action":"boom"})");
  const auto frames = fixture.frames();
  OAK_CHECK_EQ(frames.size(), 1u);
  OAK_CHECK(frames[0].type == MessageType::Error);
  OAK_CHECK_EQ(field(frames[0], "reason").asInt(), 9);
}

OAK_TEST(session_rejects_a_post_without_an_action) {
  Fixture fixture;
  fixture.negotiate();
  fixture.send(MessageType::Post, R"({"handle":1})");

  OAK_CHECK(fixture.frames()[0].type == MessageType::Error);
  OAK_CHECK_EQ(field(fixture.frames()[0], "reason").asInt(), 7);
}

OAK_TEST(session_accepts_a_set_in_range) {
  Fixture fixture;
  fixture.negotiate();
  fixture.send(MessageType::Set, R"({"handle":1,"param":"intensity","value":0.82})");

  const auto frames = fixture.frames();
  OAK_CHECK_EQ(frames.size(), 1u);
  OAK_CHECK(frames[0].type == MessageType::Status);
}

OAK_TEST(session_rejects_a_set_out_of_range) {
  Fixture fixture;
  fixture.negotiate();

  fixture.send(MessageType::Set, R"({"handle":1,"param":"intensity","value":1.5})");
  OAK_CHECK(fixture.frames()[0].type == MessageType::Error);
  OAK_CHECK_EQ(field(fixture.frames()[0], "reason").asInt(), 7);

  fixture.sink.clear();
  fixture.send(MessageType::Set, R"({"handle":1,"param":"intensity","value":-0.1})");
  OAK_CHECK(fixture.frames()[0].type == MessageType::Error);
}

OAK_TEST(session_rejects_a_non_finite_set_value) {
  // An overflowing literal parses to infinity, which is outside [0, 1] by the
  // comparison that also rejects NaN.
  Fixture fixture;
  fixture.negotiate();
  fixture.send(MessageType::Set, R"({"handle":1,"param":"intensity","value":1e999})");

  OAK_CHECK(fixture.frames()[0].type == MessageType::Error);
  OAK_CHECK_EQ(field(fixture.frames()[0], "reason").asInt(), 7);
}

OAK_TEST(session_rejects_a_set_without_a_value) {
  Fixture fixture;
  fixture.negotiate();
  fixture.send(MessageType::Set, R"({"handle":1,"param":"intensity"})");

  OAK_CHECK(fixture.frames()[0].type == MessageType::Error);
  OAK_CHECK_EQ(field(fixture.frames()[0], "reason").asInt(), 7);
}

OAK_TEST(session_rejects_a_zero_or_missing_handle) {
  Fixture fixture;
  fixture.negotiate();

  // section 5: handles are opaque and non-zero.
  fixture.send(MessageType::Post, R"({"handle":0,"action":"x"})");
  OAK_CHECK(fixture.frames()[0].type == MessageType::Error);
  OAK_CHECK_EQ(field(fixture.frames()[0], "reason").asInt(), 7);

  fixture.sink.clear();
  fixture.send(MessageType::Post, R"({"action":"x"})");
  OAK_CHECK(fixture.frames()[0].type == MessageType::Error);

  fixture.sink.clear();
  fixture.send(MessageType::Post, R"({"handle":-1,"action":"x"})");
  OAK_CHECK(fixture.frames()[0].type == MessageType::Error);

  fixture.sink.clear();
  fixture.send(MessageType::Post, R"({"handle":"1","action":"x"})");
  OAK_CHECK(fixture.frames()[0].type == MessageType::Error);
}

OAK_TEST(session_close_is_idempotent) {
  Fixture fixture;
  fixture.negotiate();

  // section 5: closing an unknown handle is not an error, so this must succeed even
  // though no target was ever opened.
  fixture.send(MessageType::Close, R"({"handle":999})");
  const auto frames = fixture.frames();
  OAK_CHECK_EQ(frames.size(), 1u);
  OAK_CHECK(frames[0].type == MessageType::Status);
  OAK_CHECK_EQ(field(frames[0], "reason").asInt(), 0);
}

OAK_TEST(session_rejects_a_reply_direction_message) {
  Fixture fixture;
  fixture.negotiate();
  fixture.send(MessageType::CapsReply, R"({"classes":[]})");

  const auto frames = fixture.frames();
  OAK_CHECK_EQ(frames.size(), 1u);
  OAK_CHECK(frames[0].type == MessageType::Error);
  OAK_CHECK_EQ(field(frames[0], "reason").asInt(), 5);
}

OAK_TEST(session_rejects_a_second_hello) {
  Fixture fixture;
  fixture.negotiate();
  fixture.send(MessageType::Hello, R"({"revisions":[1]})");

  OAK_CHECK(fixture.frames()[0].type == MessageType::Error);
  OAK_CHECK(!fixture.session.finished());
}

// ---------------------------------------------------------------- diagnostics

OAK_TEST(session_requires_opt_in_for_diagnostics) {
  Fixture fixture;
  fixture.negotiate();
  fixture.send(MessageType::DiagRequest, R"({})");

  const auto frames = fixture.frames();
  OAK_CHECK_EQ(frames.size(), 1u);
  OAK_CHECK(frames[0].type == MessageType::Error);
  OAK_CHECK_EQ(field(frames[0], "reason").asInt(), 10);
  OAK_CHECK(!fixture.session.diagnosticsEnabled());
}

OAK_TEST(session_enables_diagnostics_and_reports_redacted_facts) {
  Fixture fixture;
  fixture.negotiate();
  fixture.send(MessageType::DiagRequest, R"({"enable":true})");

  const auto frames = fixture.frames();
  OAK_CHECK_EQ(frames.size(), 1u);
  OAK_CHECK(frames[0].type == MessageType::DiagReply);
  OAK_CHECK_EQ(field(frames[0], "diagnose").asBool(), true);
  OAK_CHECK_EQ(field(frames[0], "identify").asBool(), false);

  // section 8: identification is a separate, off-by-default switch. A host that never
  // sets it can run for years without learning what it is talking to.
  const auto payload = parse(frames[0].payload);
  OAK_CHECK(payload.has_value());
  OAK_CHECK(payload->find("path") == nullptr);
  OAK_CHECK(payload->find("version") == nullptr);
  OAK_CHECK(payload->find("vendor") == nullptr);
  OAK_CHECK(payload->find("installation") == nullptr);

  // Counts and classes are safe: they describe behaviour, not a build.
  OAK_CHECK_EQ(field(frames[0], "targets").asInt(), 2);
  OAK_CHECK_EQ(field(frames[0], "symbols_resolved").asInt(), 7);
  OAK_CHECK_EQ(field(frames[0], "classes").asArray().size(), 2u);

  fixture.sink.clear();
  fixture.send(MessageType::DiagRequest, R"({})");
  OAK_CHECK(fixture.frames()[0].type == MessageType::DiagReply);
}

OAK_TEST(session_requires_diagnostics_before_identify) {
  Fixture fixture;
  fixture.negotiate();
  fixture.send(MessageType::DiagRequest, R"({"identify":true})");

  const auto frames = fixture.frames();
  OAK_CHECK_EQ(frames.size(), 1u);
  OAK_CHECK(frames[0].type == MessageType::Error);
  OAK_CHECK_EQ(field(frames[0], "reason").asInt(), 10);
  OAK_CHECK(!fixture.session.identifyEnabled());
}

OAK_TEST(session_lets_both_switches_be_turned_on_together) {
  Fixture fixture;
  fixture.negotiate();
  fixture.send(MessageType::DiagRequest, R"({"enable":true,"identify":true})");

  OAK_CHECK_EQ(field(fixture.frames()[0], "identify").asBool(), true);
  OAK_CHECK(fixture.session.identifyEnabled());
}

OAK_TEST(session_diagnostics_can_be_turned_off_again) {
  Fixture fixture;
  fixture.negotiate();

  fixture.send(MessageType::DiagRequest, R"({"enable":true,"identify":true})");
  OAK_CHECK(fixture.session.identifyEnabled());

  fixture.sink.clear();
  fixture.send(MessageType::DiagRequest, R"({"enable":false})");
  OAK_CHECK(!fixture.session.diagnosticsEnabled());
  // Turning off diagnosis must also withdraw identification.
  OAK_CHECK(!fixture.session.identifyEnabled());

  fixture.sink.clear();
  fixture.send(MessageType::DiagRequest, R"({})");
  OAK_CHECK(fixture.frames()[0].type == MessageType::Error);
  OAK_CHECK_EQ(field(fixture.frames()[0], "reason").asInt(), 10);
}

// ------------------------------------------------------- the shipping backend

OAK_TEST(session_with_no_engine_reports_nothing_and_refuses_every_open) {
  // This is what the station actually does today. It is not a stub that lies:
  // reporting a guessed class here would violate section 10, and over-claiming is the
  // failure mode that crashes at the user's expense.
  NullBackend backend;
  MemorySink sink;
  Session session(backend, sink);

  session.handle(makeFrame(MessageType::Hello, R"({"revisions":[1]})"));
  session.handle(makeFrame(MessageType::CapsRequest, "{}"));
  session.handle(makeFrame(MessageType::Open, R"({"kind":"emitter","name":"ui"})"));

  const auto frames = sink.frames();
  OAK_CHECK_EQ(frames.size(), 3u);
  OAK_CHECK(frames[0].type == MessageType::HelloAck);

  OAK_CHECK(frames[1].type == MessageType::CapsReply);
  OAK_CHECK_EQ(field(frames[1], "classes").asArray().size(), 0u);
  OAK_CHECK_EQ(field(frames[1], "bulk").asBool(), false);

  OAK_CHECK(frames[2].type == MessageType::Error);
  OAK_CHECK_EQ(field(frames[2], "reason").asInt(), 1);
}

OAK_TEST(session_never_names_a_vendor_or_version_in_a_reason) {
  // Every reason phrase is drawn from a fixed table, so no amount of
  // discovered detail can leak into one. This asserts the table stays generic.
  for (int code = 0; code <= 10; ++code) {
    const char* summary = oak::relay::protocol::reasonSummary(static_cast<Reason>(code));
    const std::string text(summary);
    OAK_CHECK(!text.empty());
    // No dotted numeric components, which is how a version would look.
    OAK_CHECK(text.find('.') == std::string::npos);
  }
}
