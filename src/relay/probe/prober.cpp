#include "relay/probe/prober.hpp"

#include <algorithm>
#include <utility>

namespace oak::relay::probe {
namespace {

// Converting an object pointer to a function pointer is conditionally supported
// rather than standard, but every platform this relay targets defines it, and
// it is the only way to reach a symbol the platform hands back as an address.
//
// What matters is where the shape comes from. It is not inferred here and not
// compiled in: the profile said what it is, and the profile came from the host.
using LevelFunction = void (*)(double);
using NameFunction = void (*)(const char*);
using RegisterSinkFunction = void (*)(bulk::Sink, void*);

}  // namespace

BoundEngine::BoundEngine(binding::DynamicLibrary library, std::vector<Profile> profiles)
    : library_(std::move(library)), profiles_(std::move(profiles)) {
  for (const Profile& profile : profiles_) {
    for (const Binding& binding : profile.bindings) {
      void* address = library_.resolve(binding.symbol);
      if (address == nullptr) continue;

      // One address per role. Two profiles naming different symbols for the
      // same role is resolved by file order, and only the first is reachable --
      // which is why bindingFor() uses the same order.
      const bool alreadyBound =
          std::any_of(addresses_.begin(), addresses_.end(),
                      [&binding](const std::pair<Role, void*>& entry) {
                        return entry.first == binding.role;
                      });
      if (!alreadyBound) {
        addresses_.emplace_back(binding.role, address);
      }
    }
  }
}

std::vector<std::string> BoundEngine::capabilityClasses() const {
  std::vector<std::string> classes;
  classes.reserve(profiles_.size());
  for (const Profile& profile : profiles_) {
    classes.push_back(profile.capabilityClass);
  }
  return classes;
}

const Binding* BoundEngine::bindingFor(Role role) const {
  for (const Profile& profile : profiles_) {
    if (const Binding* binding = profile.bindingFor(role)) return binding;
  }
  return nullptr;
}

void* BoundEngine::addressFor(Role role, SymbolShape requiredShape) const {
  const Binding* binding = bindingFor(role);
  if (binding == nullptr || binding->shape != requiredShape) return nullptr;

  for (const auto& entry : addresses_) {
    if (entry.first == role) return entry.second;
  }
  return nullptr;
}

bool BoundEngine::callWithLevel(Role role, double value) const {
  void* address = addressFor(role, SymbolShape::Level);
  if (address == nullptr) return false;

  reinterpret_cast<LevelFunction>(address)(value);
  return true;
}

bool BoundEngine::callWithName(Role role, const std::string& name) const {
  void* address = addressFor(role, SymbolShape::Name);
  if (address == nullptr) return false;

  reinterpret_cast<NameFunction>(address)(name.c_str());
  return true;
}

bool BoundEngine::installSink(bulk::Sink sink, void* context) const {
  void* address = addressFor(Role::Bulk, SymbolShape::Sink);
  if (address == nullptr) return false;

  reinterpret_cast<RegisterSinkFunction>(address)(sink, context);
  return true;
}

BindingAttempt bindFirstAvailable(const std::vector<discovery::Candidate>& candidates,
                                  const std::vector<Profile>& profiles) {
  BindingAttempt attempt;
  if (profiles.empty()) return attempt;

  for (const discovery::Candidate& candidate : candidates) {
    protocol::Reason reason = protocol::Reason::Ok;
    binding::DynamicLibrary library = binding::DynamicLibrary::load(candidate.path, reason);
    if (!library.isLoaded()) {
      // Discovery works from container format alone, so a file that is a module
      // by its bytes and not by anything else is exactly the case this skips.
      // It is not a failure worth reporting.
      continue;
    }

    ++attempt.candidatesLoaded;

    std::vector<Profile> satisfied;
    for (const Profile& profile : profiles) {
      ++attempt.profilesTried;

      bool complete = true;
      for (const Binding& binding : profile.bindings) {
        if (library.resolve(binding.symbol) == nullptr) {
          complete = false;
          break;
        }
      }
      if (complete) satisfied.push_back(profile);
    }

    if (satisfied.empty()) continue;

    attempt.engine = BoundEngine(std::move(library), std::move(satisfied));
    return attempt;
  }

  return attempt;
}

}  // namespace oak::relay::probe
