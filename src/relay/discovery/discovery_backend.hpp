// A backend that looks, and then honestly reports that it recognised nothing.
//
// This is what the relay station runs on today.
//
// The distinction it exists to preserve is the one in protocol section 10:
// recognising a container format is not the same as knowing a calling
// convention. A file being a loadable module says the platform could load it;
// it says nothing about what to call inside it. So this backend examines files,
// counts them, and offers no compatibility classes -- because offering one it
// could not honour would crash on a user's machine, and under-claiming merely
// means the host does less.
//
// The scan is lazy. It runs the first time the host asks what the station can
// do -- on the capability request, not at startup: a relay that walked the disk
// before being asked would be doing the unprompted surveying this whole project
// exists to avoid.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "relay/backend.hpp"
#include "relay/discovery/scanner.hpp"

namespace oak::relay::discovery {

/// Runs a bounded, host-directed scan and reports what it examined.
class DiscoveryBackend final : public EngineBackend {
 public:
  explicit DiscoveryBackend(std::vector<std::string> roots, ScanLimits limits = {})
      : roots_(std::move(roots)), limits_(limits) {}

  std::vector<std::string> capabilityClasses() const override {
    // Look first, so that "what can you do?" is answered by an answer rather
    // than by an assumption. Then report nothing: a container format does not
    // establish a calling convention, so there is no class this backend is
    // entitled to claim yet. See the note at the top of this file.
    (void)ensureScanned();
    return {};
  }

  protocol::json::Value limits() const override {
    protocol::json::Value value = protocol::json::Value::object();
    value.set("max_candidates",
              protocol::json::Value::integer(static_cast<std::int64_t>(limits_.maxFiles)));
    value.set("max_scan_depth",
              protocol::json::Value::integer(static_cast<std::int64_t>(limits_.maxDepth)));
    return value;
  }

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

  std::uint32_t filesExamined() const override {
    return static_cast<std::uint32_t>(ensureScanned().filesExamined);
  }

  std::uint32_t candidatesFound() const override {
    return static_cast<std::uint32_t>(ensureScanned().candidates.size());
  }

  bool discoveryTruncated() const override { return ensureScanned().truncated; }

  /// Candidates found but not claimed. Exposed for the station's own tests; the
  /// paths inside are never logged, transmitted, or written anywhere.
  const std::vector<Candidate>& candidates() const { return ensureScanned().candidates; }

 private:
  const ScanResult& ensureScanned() const {
    if (!scanned_) {
      result_ = scanForCandidates(roots_, limits_);
      scanned_ = true;
    }
    return result_;
  }

  std::vector<std::string> roots_;
  ScanLimits limits_;

  // Mutable because the scan is a cache, not a state change: the answer does
  // not depend on when it is asked.
  mutable bool scanned_ = false;
  mutable ScanResult result_;
};

}  // namespace oak::relay::discovery
