// What the host tells the relay about how to talk to an engine.
//
// This file is the answer to the hardest question in the project: how can a
// relay bind an engine it has never heard of, without being shipped knowing
// about one?
//
// The answer is that the host describes the calling shape, at run time, as
// data. Nothing in this build names a product, a version, or a symbol. The
// relay is told what to look for; it never knows what it found.
//
// That keeps three axioms intact at once. A1, because no product appears. A2,
// because no symbol name is compiled in. A3, because what is matched is a
// *shape*, and a shape outlives the build that produced it.
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace oak::relay::probe {

/// How the relay will call a resolved symbol.
///
/// This is the entire vocabulary of calling conventions the relay ships, and it
/// is deliberately tiny. Each entry is a *shape*, not an API: nothing here says
/// what a function does, which product provides it, or what version it is.
///
/// A richer vocabulary would be an API, and an API the relay ships is an API
/// the relay is coupled to. Adding a shape is therefore a decision to be argued
/// for, not a convenience to be reached for.
///
/// There is deliberately no "takes nothing" shape. The vocabulary contains only
/// the shapes the protocol can actually deliver -- POST carries a name, SET
/// carries a value, and the bulk channel needs somewhere to put audio -- so a
/// shape nothing can be called with is dead weight, and worse: a profile could
/// ask for one and be accepted.
enum class SymbolShape {
  Level,  ///< void (*)(double)
  Name,   ///< void (*)(const char*)
  /// void (*)(bulk::Sink, void*)
  ///
  /// The only shape that is called *back*. Installing a sink is what makes
  /// section 7's channel reachable: the relay provides somewhere for audio to
  /// go, and the engine decides when to send it.
  Sink,
};

const char* symbolShapeName(SymbolShape shape);
std::optional<SymbolShape> symbolShapeFromName(const std::string& name);

/// The protocol operations a profile can bind an entry point to.
enum class Role {
  Post,  ///< POST: fire a named action at a target.
  Set,   ///< SET: set a continuous parameter.
  Bulk,  ///< Install the audio sink of protocol section 7.
};

const char* roleName(Role role);
std::optional<Role> roleFromName(const std::string& name);

/// One entry point a profile needs, and how it will be called.
struct Binding {
  Role role = Role::Post;
  std::string symbol;
  SymbolShape shape = SymbolShape::Level;
};

/// A statement of what a module must provide to be usable.
struct Profile {
  std::string capabilityClass;
  std::vector<Binding> bindings;

  /// The binding declared for [role], or nullptr when there is none.
  const Binding* bindingFor(Role role) const;
};

/// Parses a profile document.
///
/// Returns nullopt when the document is malformed or self-contradictory. A
/// profile that cannot be understood is refused outright rather than applied in
/// part: half a description of how to call somebody's code is worse than none
/// at all, because the half that is missing is the half that would have crashed.
std::optional<std::vector<Profile>> parseProfiles(const std::string& jsonText);

/// Reads and parses a profile file.
///
/// The caller owns the path. It is read once and not stored, so a nominated
/// profile leaves no trace in this process beyond the profiles themselves.
std::optional<std::vector<Profile>> loadProfiles(const std::string& path);

}  // namespace oak::relay::probe
