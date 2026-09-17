#include "harness.hpp"

#include <string>

#include "relay/probe/profile.hpp"

namespace {

using oak::relay::probe::loadProfiles;
using oak::relay::probe::parseProfiles;
using oak::relay::probe::Profile;
using oak::relay::probe::Role;
using oak::relay::probe::roleFromName;
using oak::relay::probe::roleName;
using oak::relay::probe::SymbolShape;
using oak::relay::probe::symbolShapeFromName;
using oak::relay::probe::symbolShapeName;

constexpr const char* kValid = R"({
  "profiles": [
    {
      "class": "engine.param.continuous",
      "bindings": [
        { "role": "set",  "symbol": "some_symbol", "shape": "level" },
        { "role": "post", "symbol": "other_symbol", "shape": "name" }
      ]
    }
  ]
})";

}  // namespace

OAK_TEST(profile_parses_a_well_formed_document) {
  const auto profiles = parseProfiles(kValid);
  OAK_CHECK(profiles.has_value());
  OAK_CHECK_EQ(profiles->size(), 1u);

  const Profile& profile = profiles->front();
  OAK_CHECK_EQ(profile.capabilityClass, std::string("engine.param.continuous"));
  OAK_CHECK_EQ(profile.bindings.size(), 2u);

  const auto* set = profile.bindingFor(Role::Set);
  OAK_CHECK(set != nullptr);
  OAK_CHECK_EQ(set->symbol, std::string("some_symbol"));
  OAK_CHECK(set->shape == SymbolShape::Level);

  const auto* post = profile.bindingFor(Role::Post);
  OAK_CHECK(post != nullptr);
  OAK_CHECK_EQ(post->symbol, std::string("other_symbol"));
  OAK_CHECK(post->shape == SymbolShape::Name);
}

OAK_TEST(profile_reports_a_role_it_does_not_bind) {
  const auto profiles = parseProfiles(
      R"({"profiles":[{"class":"c","bindings":[{"role":"set","symbol":"s","shape":"level"}]}]})");
  OAK_CHECK(profiles.has_value());
  OAK_CHECK(profiles->front().bindingFor(Role::Set) != nullptr);
  // Not an error: a profile binds what it binds, and nothing more.
  OAK_CHECK(profiles->front().bindingFor(Role::Post) == nullptr);
}

OAK_TEST(profile_refuses_documents_it_cannot_understand) {
  OAK_CHECK(!parseProfiles("not json at all").has_value());
  OAK_CHECK(!parseProfiles("{}").has_value());
  OAK_CHECK(!parseProfiles(R"({"profiles":{}})").has_value());
  OAK_CHECK(!parseProfiles(R"({"profiles":[]})").has_value());

  // No class, and an empty class, are both refusals.
  OAK_CHECK(!parseProfiles(
                 R"({"profiles":[{"bindings":[{"role":"set","symbol":"s","shape":"level"}]}]})")
                 .has_value());
  OAK_CHECK(!parseProfiles(
                 R"({"profiles":[{"class":"","bindings":[{"role":"set","symbol":"s","shape":"level"}]}]})")
                 .has_value());

  // Missing bindings, and empty bindings. The second matters most: a profile
  // requiring nothing would be satisfied by any file at all, including a file
  // that contains nothing, and that is not a capability claim.
  OAK_CHECK(!parseProfiles(R"({"profiles":[{"class":"c"}]})").has_value());
  OAK_CHECK(!parseProfiles(R"({"profiles":[{"class":"c","bindings":[]}]})").has_value());

  // Unknown role, unknown shape, empty symbol.
  OAK_CHECK(!parseProfiles(
                 R"({"profiles":[{"class":"c","bindings":[{"role":"teleport","symbol":"s","shape":"level"}]}]})")
                 .has_value());
  OAK_CHECK(!parseProfiles(
                 R"({"profiles":[{"class":"c","bindings":[{"role":"set","symbol":"s","shape":"int"}]}]})")
                 .has_value());
  OAK_CHECK(!parseProfiles(
                 R"({"profiles":[{"class":"c","bindings":[{"role":"set","symbol":"","shape":"level"}]}]})")
                 .has_value());

  // There is deliberately no shape that takes nothing, so a profile asking for
  // one is refused rather than accepted and never called.
  OAK_CHECK(!parseProfiles(
                 R"({"profiles":[{"class":"c","bindings":[{"role":"set","symbol":"s","shape":"none"}]}]})")
                 .has_value());
}

OAK_TEST(profile_refuses_two_bindings_for_one_role) {
  // Guessing which one the host meant is how a relay calls the wrong function.
  const auto profiles = parseProfiles(
      R"({"profiles":[{"class":"c","bindings":[{"role":"set","symbol":"a","shape":"level"},{"role":"set","symbol":"b","shape":"level"}]}]})");
  OAK_CHECK(!profiles.has_value());
}

OAK_TEST(profile_parses_several_profiles_in_order) {
  const auto profiles = parseProfiles(R"({
    "profiles": [
      { "class": "first",  "bindings": [ { "role": "set", "symbol": "a", "shape": "level" } ] },
      { "class": "second", "bindings": [ { "role": "post", "symbol": "b", "shape": "name" } ] }
    ]
  })");
  OAK_CHECK(profiles.has_value());
  OAK_CHECK_EQ(profiles->size(), 2u);
  // File order is preserved, because it is what decides which profile wins.
  OAK_CHECK_EQ((*profiles)[0].capabilityClass, std::string("first"));
  OAK_CHECK_EQ((*profiles)[1].capabilityClass, std::string("second"));
}

OAK_TEST(profile_names_round_trip) {
  OAK_CHECK(roleFromName(roleName(Role::Post)) == Role::Post);
  OAK_CHECK(roleFromName(roleName(Role::Set)) == Role::Set);
  OAK_CHECK(symbolShapeFromName(symbolShapeName(SymbolShape::Level)) == SymbolShape::Level);
  OAK_CHECK(symbolShapeFromName(symbolShapeName(SymbolShape::Name)) == SymbolShape::Name);

  OAK_CHECK(!roleFromName("teleport").has_value());
  OAK_CHECK(!symbolShapeFromName("none").has_value());
}

OAK_TEST(profile_load_reports_a_file_it_cannot_read) {
  OAK_CHECK(!loadProfiles("").has_value());
  OAK_CHECK(!loadProfiles("no/such/profile/document.json").has_value());
}
