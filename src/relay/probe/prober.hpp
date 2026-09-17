// Probing: does this module satisfy this description, and can we call it?
//
// Probing is the stage where a file that looked promising becomes either a
// capability the relay can honour or a file it declines to claim anything
// about. Both outcomes are normal. The second is the common one.
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "relay/binding/dynamic_library.hpp"
#include "relay/discovery/scanner.hpp"
#include "relay/probe/profile.hpp"

namespace oak::relay::probe {

/// An engine the relay managed to bind: a loaded module, plus every profile it
/// satisfied.
///
/// The relay knows nothing else about it. Not what it is called, not what
/// version it is, not where it lives -- the path was consumed by the loader and
/// is not kept, which is what makes axiom A4 enforceable here rather than
/// aspirational.
class BoundEngine {
 public:
  BoundEngine() = default;
  BoundEngine(binding::DynamicLibrary library, std::vector<Profile> profiles);

  bool isBound() const { return library_.isLoaded() && !profiles_.empty(); }
  explicit operator bool() const { return isBound(); }

  /// Every profile this module satisfied, in the order the file listed them.
  const std::vector<Profile>& profiles() const { return profiles_; }

  /// The classes the relay may now offer: all of them, and only them.
  ///
  /// Each was verified by resolving every symbol it names, so offering it is a
  /// statement of fact rather than of hope. A class that is not here is one the
  /// relay could not prove, and an unproven capability is an absent one.
  std::vector<std::string> capabilityClasses() const;

  /// How many symbols were resolved to satisfy this binding.
  ///
  /// Section 8: a count, never a list of names. A resolved symbol name is
  /// exactly the kind of fact that identifies what is on the other end.
  std::size_t resolvedSymbolCount() const { return addresses_.size(); }

  /// Calls the binding declared for [role] with a continuous value.
  ///
  /// Returns false when no satisfied profile declares a binding for the role,
  /// or declares one whose shape cannot carry the argument.
  bool callWithLevel(Role role, double value) const;

  /// Calls the binding declared for [role] with a name.
  bool callWithName(Role role, const std::string& name) const;

 private:
  /// The first satisfied profile that declares [role], in file order.
  ///
  /// First rather than "best": a relay that ranked two profiles would be
  /// expressing a preference it has no basis for.
  const Binding* bindingFor(Role role) const;

  void* addressFor(Role role, SymbolShape requiredShape) const;

  binding::DynamicLibrary library_;
  std::vector<Profile> profiles_;
  std::vector<std::pair<Role, void*>> addresses_;
};

/// What a binding attempt concluded.
struct BindingAttempt {
  BoundEngine engine;

  /// Candidates whose module the platform agreed to load.
  std::size_t candidatesLoaded = 0;

  /// Candidate-and-profile pairs actually evaluated.
  std::size_t profilesTried = 0;
};

/// Tries each candidate in turn and binds the first one that satisfies at least
/// one profile.
///
/// Order is deterministic: candidates in scan order, profiles in file order.
/// A host that cares which module gets chosen therefore controls it by ordering
/// its own search roots -- rather than by the relay inventing a preference,
/// which would mean the relay knowing something it has no business knowing.
///
/// Every profile the chosen module satisfies is bound, not just the first. One
/// module routinely provides more than one capability, and reporting only one
/// would be under-claiming for no reason.
///
/// A candidate the platform refuses to load is skipped silently. Discovery
/// works from container format alone, and this is precisely the case where the
/// format was right and nothing else was; it is not a failure worth reporting.
BindingAttempt bindFirstAvailable(const std::vector<discovery::Candidate>& candidates,
                                  const std::vector<Profile>& profiles);

}  // namespace oak::relay::probe
