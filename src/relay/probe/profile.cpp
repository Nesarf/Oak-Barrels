#include "relay/probe/profile.hpp"

#include <fstream>
#include <sstream>
#include <utility>

#include "relay/protocol/json.hpp"

namespace oak::relay::probe {
namespace {

using protocol::json::Value;

constexpr const char* kPostRole = "post";
constexpr const char* kSetRole = "set";

constexpr const char* kLevelShape = "level";
constexpr const char* kNameShape = "name";

}  // namespace

const char* symbolShapeName(SymbolShape shape) {
  switch (shape) {
    case SymbolShape::Level:
      return kLevelShape;
    case SymbolShape::Name:
      return kNameShape;
  }
  return "unknown";
}

std::optional<SymbolShape> symbolShapeFromName(const std::string& name) {
  if (name == kLevelShape) return SymbolShape::Level;
  if (name == kNameShape) return SymbolShape::Name;
  return std::nullopt;
}

const char* roleName(Role role) {
  switch (role) {
    case Role::Post:
      return kPostRole;
    case Role::Set:
      return kSetRole;
  }
  return kPostRole;
}

std::optional<Role> roleFromName(const std::string& name) {
  if (name == kPostRole) return Role::Post;
  if (name == kSetRole) return Role::Set;
  return std::nullopt;
}

const Binding* Profile::bindingFor(Role role) const {
  for (const Binding& binding : bindings) {
    if (binding.role == role) return &binding;
  }
  return nullptr;
}

std::optional<std::vector<Profile>> parseProfiles(const std::string& jsonText) {
  const auto document = protocol::json::parse(jsonText);
  if (!document.has_value() || !document->isObject()) return std::nullopt;

  const Value* rawProfiles = document->find("profiles");
  if (rawProfiles == nullptr || !rawProfiles->isArray()) return std::nullopt;

  std::vector<Profile> profiles;

  for (const Value& entry : rawProfiles->asArray()) {
    if (!entry.isObject()) return std::nullopt;

    const Value* rawClass = entry.find("class");
    if (rawClass == nullptr || !rawClass->isString() || rawClass->asString().empty()) {
      return std::nullopt;
    }

    const Value* rawBindings = entry.find("bindings");
    if (rawBindings == nullptr || !rawBindings->isArray() || rawBindings->asArray().empty()) {
      // A profile that requires nothing would be satisfied by any file at all,
      // including a file that contains nothing. That is not a capability claim;
      // it is a bug waiting to be reported as one.
      return std::nullopt;
    }

    Profile profile;
    profile.capabilityClass = rawClass->asString();

    for (const Value& rawBinding : rawBindings->asArray()) {
      if (!rawBinding.isObject()) return std::nullopt;

      const Value* rawRole = rawBinding.find("role");
      const Value* rawSymbol = rawBinding.find("symbol");
      const Value* rawShape = rawBinding.find("shape");

      if (rawRole == nullptr || !rawRole->isString()) return std::nullopt;
      if (rawShape == nullptr || !rawShape->isString()) return std::nullopt;
      if (rawSymbol == nullptr || !rawSymbol->isString() || rawSymbol->asString().empty()) {
        return std::nullopt;
      }

      const auto role = roleFromName(rawRole->asString());
      const auto shape = symbolShapeFromName(rawShape->asString());
      if (!role.has_value() || !shape.has_value()) return std::nullopt;

      if (profile.bindingFor(*role) != nullptr) {
        // Two bindings for one role is an ambiguity, and guessing which one the
        // host meant is how a relay ends up calling the wrong function.
        return std::nullopt;
      }

      Binding binding;
      binding.role = *role;
      binding.symbol = rawSymbol->asString();
      binding.shape = *shape;
      profile.bindings.push_back(std::move(binding));
    }

    profiles.push_back(std::move(profile));
  }

  if (profiles.empty()) return std::nullopt;
  return profiles;
}

std::optional<std::vector<Profile>> loadProfiles(const std::string& path) {
  if (path.empty()) return std::nullopt;

  std::ifstream stream(path, std::ios::binary);
  if (!stream) return std::nullopt;

  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return parseProfiles(buffer.str());
}

}  // namespace oak::relay::probe
