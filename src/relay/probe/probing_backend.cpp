#include "relay/probe/probing_backend.hpp"

#include <algorithm>

namespace oak::relay::probe {

ProbingBackend::ProbingBackend(std::vector<std::string> roots,
                               std::vector<Profile> profiles,
                               discovery::ScanLimits limits)
    : roots_(std::move(roots)), profiles_(std::move(profiles)), limits_(limits) {}

const BindingAttempt& ProbingBackend::ensureProbed() const {
  if (!probed_) {
    scan_ = discovery::scanForCandidates(roots_, limits_);
    attempt_ = bindFirstAvailable(scan_.candidates, profiles_);
    probed_ = true;
  }
  return attempt_;
}

std::vector<std::string> ProbingBackend::capabilityClasses() const {
  const BindingAttempt& attempt = ensureProbed();

  // Every class here was proved by resolving every symbol it names. Nothing
  // else is offered: an unverified capability is an absent one (A5, section 10).
  return attempt.engine.capabilityClasses();
}

protocol::json::Value ProbingBackend::limits() const {
  protocol::json::Value value = protocol::json::Value::object();
  value.set("max_candidates",
            protocol::json::Value::integer(static_cast<std::int64_t>(limits_.maxFiles)));
  value.set("max_scan_depth",
            protocol::json::Value::integer(static_cast<std::int64_t>(limits_.maxDepth)));
  return value;
}

std::uint32_t ProbingBackend::openTarget(const std::string& /*kind*/,
                                         const std::string& /*name*/,
                                         protocol::Reason& reason) {
  const BindingAttempt& attempt = ensureProbed();
  if (!attempt.engine.isBound()) {
    reason = protocol::Reason::NoEngine;
    return 0;
  }

  // A handle is this relay's own bookkeeping and nothing more. A profile
  // describes entry points, not target lifetime, so nothing is created in the
  // module here -- inventing a lifetime convention would be inventing an API.
  const std::uint32_t handle = nextHandle_++;
  liveHandles_.push_back(handle);
  reason = protocol::Reason::Ok;
  return handle;
}

protocol::Reason ProbingBackend::closeTarget(std::uint32_t handle) {
  const auto entry = std::find(liveHandles_.begin(), liveHandles_.end(), handle);
  if (entry != liveHandles_.end()) liveHandles_.erase(entry);
  return protocol::Reason::Ok;  // idempotent by contract (section 5)
}

protocol::Reason ProbingBackend::post(std::uint32_t handle,
                                      const std::string& action,
                                      const protocol::json::Value& /*args*/) {
  const BindingAttempt& attempt = ensureProbed();
  if (!attempt.engine.isBound()) return protocol::Reason::NoEngine;

  if (std::find(liveHandles_.begin(), liveHandles_.end(), handle) == liveHandles_.end()) {
    return protocol::Reason::InvalidHandle;
  }

  if (!attempt.engine.callWithName(Role::Post, action)) {
    // Bound, but this profile declared no name-taking entry point for POST.
    // Saying "not supported" is the truthful answer; the alternative would be
    // to call something whose arguments do not fit.
    return protocol::Reason::UnsupportedOperation;
  }
  return protocol::Reason::Ok;
}

protocol::Reason ProbingBackend::set(std::uint32_t handle,
                                     const std::string& /*param*/,
                                     double value) {
  const BindingAttempt& attempt = ensureProbed();
  if (!attempt.engine.isBound()) return protocol::Reason::NoEngine;

  if (std::find(liveHandles_.begin(), liveHandles_.end(), handle) == liveHandles_.end()) {
    return protocol::Reason::InvalidHandle;
  }

  if (!attempt.engine.callWithLevel(Role::Set, value)) {
    return protocol::Reason::UnsupportedOperation;
  }
  return protocol::Reason::Ok;
}

std::uint32_t ProbingBackend::filesExamined() const {
  (void)ensureProbed();
  return static_cast<std::uint32_t>(scan_.filesExamined);
}

std::uint32_t ProbingBackend::candidatesFound() const {
  (void)ensureProbed();
  return static_cast<std::uint32_t>(scan_.candidates.size());
}

bool ProbingBackend::discoveryTruncated() const {
  (void)ensureProbed();
  return scan_.truncated;
}

std::uint32_t ProbingBackend::resolvedSymbolCount() const {
  return static_cast<std::uint32_t>(ensureProbed().engine.resolvedSymbolCount());
}

std::uint32_t ProbingBackend::targetCount() const {
  return static_cast<std::uint32_t>(liveHandles_.size());
}

}  // namespace oak::relay::probe
