// The engine side of the relay, as the session sees it.
//
// Everything a session can ask of a backing engine goes through this
// interface. The session itself knows nothing about how an engine was found,
// which is what lets discovery and dynamic binding change without touching
// the protocol layer -- and what keeps the protocol layer testable with a
// backend that has no engine behind it at all.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "relay/protocol/json.hpp"
#include "relay/protocol/messages.hpp"

namespace oak::relay {

/// A backing engine, reduced to the operations the protocol exposes.
class EngineBackend {
 public:
  virtual ~EngineBackend() = default;

  /// Compatibility classes this backend can honour (protocol section 4.3).
  ///
  /// Classes name families of behaviour, never builds. A backend that is
  /// unsure must report a *narrower* class, never a broader one (section 10).
  virtual std::vector<std::string> capabilityClasses() const = 0;

  /// Numeric limits advertised alongside the classes.
  virtual protocol::json::Value limits() const = 0;

  /// Whether a bulk audio channel could be negotiated on a second transport (section 7).
  virtual bool bulkChannel() const { return false; }

  /// Opens a target and returns its handle, or 0 on failure.
  ///
  /// On failure `reason` receives the code to report. A successful handle must
  /// be non-zero (section 5).
  virtual std::uint32_t openTarget(const std::string& kind,
                                   const std::string& name,
                                   protocol::Reason& reason) = 0;

  /// Closes a target. Idempotent: an unknown handle is not an error (section 5).
  virtual protocol::Reason closeTarget(std::uint32_t handle) = 0;

  /// Fires a named action at a target.
  virtual protocol::Reason post(std::uint32_t handle,
                                const std::string& action,
                                const protocol::json::Value& args) = 0;

  /// Sets a continuous parameter at a target.
  virtual protocol::Reason set(std::uint32_t handle,
                               const std::string& param,
                               double value) = 0;

  /// Number of symbols resolved so far. Reported as a count, never as names (section 8).
  virtual std::uint32_t resolvedSymbolCount() const { return 0; }

  /// Number of live targets, for diagnostics.
  virtual std::uint32_t targetCount() const { return 0; }
};

/// A backend that has found no engine.
///
/// This is what the station runs on until discovery lands, and it is not a
/// placeholder that lies: it reports no classes and refuses every open with
/// `NO_ENGINE`, which is precisely what a station with nothing to talk to
/// should say. Reporting a guessed class here would violate section 10 and would be
/// the failure mode that hurts users.
class NullBackend final : public EngineBackend {
 public:
  std::vector<std::string> capabilityClasses() const override { return {}; }

  protocol::json::Value limits() const override { return protocol::json::Value::object(); }

  bool bulkChannel() const override { return false; }

  std::uint32_t openTarget(const std::string& /*kind*/,
                           const std::string& /*name*/,
                           protocol::Reason& reason) override {
    reason = protocol::Reason::NoEngine;
    return 0;
  }

  protocol::Reason closeTarget(std::uint32_t /*handle*/) override {
    return protocol::Reason::Ok;  // idempotent by contract
  }

  protocol::Reason post(std::uint32_t /*handle*/,
                        const std::string& /*action*/,
                        const protocol::json::Value& /*args*/) override {
    return protocol::Reason::NoEngine;
  }

  protocol::Reason set(std::uint32_t /*handle*/,
                       const std::string& /*param*/,
                       double /*value*/) override {
    return protocol::Reason::NoEngine;
  }
};

}  // namespace oak::relay
