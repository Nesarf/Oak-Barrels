// A backend that looks for an engine, and offers exactly what it verified.
//
// The difference between this and `DiscoveryBackend` is the whole point of the
// probe stage. Discovery says "here are some files"; this says "here is a
// capability, and here is the symbol resolution that proves it".
//
// It still claims nothing it did not verify. When no profile is satisfied it
// offers no classes and refuses every open with NO_ENGINE -- the same answer as
// having found nothing, because that is what happened.
//
// One consequence deserves stating plainly, because it is a real cost and not a
// detail: **probing maps the module into this process**, so its initialisation
// code runs here. That is inherent to what binding means -- an engine has to
// live in some process, and section 7 says the relay is not the audio path, so
// whoever hosts it must be chosen deliberately. It happens only for search
// roots and profiles the host nominated, and never at startup.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "relay/backend.hpp"
#include "relay/discovery/scanner.hpp"
#include "relay/probe/prober.hpp"
#include "relay/probe/profile.hpp"

namespace oak::relay::probe {

/// Runs a bounded, host-directed scan, probes it against the host's profiles,
/// and reports the classes it could verify.
class ProbingBackend final : public EngineBackend {
 public:
  ProbingBackend(std::vector<std::string> roots,
                 std::vector<Profile> profiles,
                 discovery::ScanLimits limits = {});

  std::vector<std::string> capabilityClasses() const override;
  protocol::json::Value limits() const override;
  bool bulkChannel() const override { return false; }

  std::uint32_t openTarget(const std::string& kind,
                           const std::string& name,
                           protocol::Reason& reason) override;

  protocol::Reason closeTarget(std::uint32_t handle) override;

  protocol::Reason post(std::uint32_t handle,
                        const std::string& action,
                        const protocol::json::Value& args) override;

  protocol::Reason set(std::uint32_t handle,
                       const std::string& param,
                       double value) override;

  std::uint32_t filesExamined() const override;
  std::uint32_t candidatesFound() const override;
  bool discoveryTruncated() const override;
  std::uint32_t resolvedSymbolCount() const override;
  std::uint32_t targetCount() const override;

  /// Number of candidates the platform agreed to load.
  std::uint32_t candidatesLoaded() const { return static_cast<std::uint32_t>(ensureProbed().candidatesLoaded); }

  /// Whether any profile was satisfied. Exposed for the station's own tests.
  bool isBound() const { return ensureProbed().engine.isBound(); }

 private:
  /// Scans and probes on first use, and remembers the answer.
  ///
  /// Lazy because the alternative is loading somebody's module before being
  /// asked to, which is the unprompted surveying A4 exists to prevent.
  const BindingAttempt& ensureProbed() const;

  std::vector<std::string> roots_;
  std::vector<Profile> profiles_;
  discovery::ScanLimits limits_;

  // Mutable because probing is a cache, not a state change: the answer does not
  // depend on when it is asked.
  mutable bool probed_ = false;
  mutable discovery::ScanResult scan_;
  mutable BindingAttempt attempt_;

  std::uint32_t nextHandle_ = 1;
  std::vector<std::uint32_t> liveHandles_;
};

}  // namespace oak::relay::probe
