// Proving the whole chain: scan a directory, recognise a module by its
// container format, resolve the symbols a profile names, and call them.
//
// Nothing here links the fixture. The test loads it the same way the relay
// does, so the load-and-resolve path is genuinely exercised rather than
// bypassed by the linker.
#include "harness.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "relay/binding/dynamic_library.hpp"
#include "relay/discovery/scanner.hpp"
#include "relay/probe/prober.hpp"
#include "relay/probe/profile.hpp"

#ifndef OAK_PROBE_FIXTURE_PATH
#error "OAK_PROBE_FIXTURE_PATH must be defined by the build"
#endif

namespace {

namespace fs = std::filesystem;

using oak::relay::binding::DynamicLibrary;
using oak::relay::discovery::scanForCandidates;
using oak::relay::probe::bindFirstAvailable;
using oak::relay::probe::BindingAttempt;
using oak::relay::probe::parseProfiles;
using oak::relay::probe::Profile;
using oak::relay::probe::Role;
using oak::relay::protocol::Reason;

/// A directory holding one copy of the fixture, under a name that says nothing.
///
/// The copy matters. Probing works on what a file exports, not on what it is
/// called, and the relay never learns the difference -- so the test should not
/// be able to rely on the name either.
class FixtureTree {
 public:
  FixtureTree() {
    static int counter = 0;
    std::error_code error;
    path_ = fs::temp_directory_path(error) /
            ("oak-probe-fixture-" + std::to_string(counter++));
    fs::remove_all(path_, error);
    fs::create_directories(path_, error);

    const fs::path source(OAK_PROBE_FIXTURE_PATH);
    modulePath_ = path_ / source.filename();
    fs::copy_file(source, modulePath_, fs::copy_options::overwrite_existing, error);
    copied_ = !error;
  }

  ~FixtureTree() {
    std::error_code error;
    fs::remove_all(path_, error);
  }

  FixtureTree(const FixtureTree&) = delete;
  FixtureTree& operator=(const FixtureTree&) = delete;

  bool usable() const { return copied_; }
  const fs::path& path() const { return path_; }
  const fs::path& modulePath() const { return modulePath_; }

 private:
  fs::path path_;
  fs::path modulePath_;
  bool copied_ = false;
};

/// Reads the fixture's observations without linking it.
///
/// Loading the same file a second time yields the same module, so these see the
/// state the bound engine wrote.
class Observer {
 public:
  explicit Observer(const std::string& path) {
    Reason reason = Reason::Ok;
    library_ = DynamicLibrary::load(path, reason);
    if (!library_.isLoaded()) return;

    level_ = reinterpret_cast<std::int64_t (*)()>(library_.resolve("oak_fixture_level"));
    triggers_ = reinterpret_cast<std::int64_t (*)()>(library_.resolve("oak_fixture_triggers"));
    lastName_ =
        reinterpret_cast<const char* (*)()>(library_.resolve("oak_fixture_last_name"));
  }

  bool usable() const {
    return level_ != nullptr && triggers_ != nullptr && lastName_ != nullptr;
  }

  std::int64_t level() const { return level_(); }
  std::int64_t triggers() const { return triggers_(); }
  std::string lastName() const { return std::string(lastName_()); }

 private:
  DynamicLibrary library_;
  std::int64_t (*level_)() = nullptr;
  std::int64_t (*triggers_)() = nullptr;
  const char* (*lastName_)() = nullptr;
};

/// The profile the fixture satisfies.
std::vector<Profile> fixtureProfiles() {
  const auto profiles = parseProfiles(R"({
    "profiles": [
      {
        "class": "engine.param.continuous",
        "bindings": [
          { "role": "set",  "symbol": "oak_fixture_set_level", "shape": "level" },
          { "role": "post", "symbol": "oak_fixture_trigger",   "shape": "name"  }
        ]
      }
    ]
  })");
  OAK_CHECK(profiles.has_value());
  return *profiles;
}

/// A profile that names symbols the fixture does not export.
std::vector<Profile> missProfiles() {
  const auto profiles = parseProfiles(R"({
    "profiles": [
      {
        "class": "engine.action.named",
        "bindings": [
          { "role": "post", "symbol": "no_such_symbol_at_all", "shape": "name" }
        ]
      }
    ]
  })");
  OAK_CHECK(profiles.has_value());
  return *profiles;
}

}  // namespace

OAK_TEST(prober_binds_a_module_that_satisfies_a_profile) {
  const FixtureTree tree;
  if (!tree.usable()) return;  // the copy failed; nothing to probe

  const auto scan = scanForCandidates({tree.path().u8string()});
  OAK_CHECK_EQ(scan.candidates.size(), 1u);

  const BindingAttempt attempt = bindFirstAvailable(scan.candidates, fixtureProfiles());
  OAK_CHECK(attempt.engine.isBound());
  OAK_CHECK_EQ(attempt.candidatesLoaded, 1u);

  const std::vector<std::string> classes = attempt.engine.capabilityClasses();
  OAK_CHECK_EQ(classes.size(), 1u);
  OAK_CHECK_EQ(classes.front(), std::string("engine.param.continuous"));

  // Section 8: a count of resolved symbols, never a list of their names.
  OAK_CHECK_EQ(attempt.engine.resolvedSymbolCount(), 2u);
}

OAK_TEST(prober_binds_every_profile_the_module_satisfies) {
  const FixtureTree tree;
  if (!tree.usable()) return;

  const auto profiles = parseProfiles(R"({
    "profiles": [
      {
        "class": "engine.param.continuous",
        "bindings": [ { "role": "set", "symbol": "oak_fixture_set_level", "shape": "level" } ]
      },
      {
        "class": "engine.action.named",
        "bindings": [ { "role": "post", "symbol": "oak_fixture_trigger", "shape": "name" } ]
      },
      {
        "class": "engine.bus.hierarchical",
        "bindings": [ { "role": "set", "symbol": "no_such_symbol_at_all", "shape": "level" } ]
      }
    ]
  })");
  OAK_CHECK(profiles.has_value());

  const auto scan = scanForCandidates({tree.path().u8string()});
  const BindingAttempt attempt = bindFirstAvailable(scan.candidates, *profiles);
  OAK_CHECK(attempt.engine.isBound());

  // All three were evaluated; only the two that were proved are offered. One
  // module routinely provides more than one capability, and reporting only the
  // first would be under-claiming for no reason.
  OAK_CHECK_EQ(attempt.profilesTried, 3u);

  const std::vector<std::string> classes = attempt.engine.capabilityClasses();
  OAK_CHECK_EQ(classes.size(), 2u);
  OAK_CHECK_EQ(classes[0], std::string("engine.param.continuous"));
  OAK_CHECK_EQ(classes[1], std::string("engine.action.named"));

  // Both roles are reachable, even though they came from different profiles.
  OAK_CHECK(attempt.engine.callWithLevel(Role::Set, 0.5));
  OAK_CHECK(attempt.engine.callWithName(Role::Post, "boom"));
}

OAK_TEST(prober_calls_the_entry_points_the_profile_named) {
  const FixtureTree tree;
  if (!tree.usable()) return;

  const Observer observer(tree.modulePath().u8string());
  if (!observer.usable()) return;

  const auto scan = scanForCandidates({tree.path().u8string()});
  const BindingAttempt attempt = bindFirstAvailable(scan.candidates, fixtureProfiles());
  OAK_CHECK(attempt.engine.isBound());

  const std::int64_t triggersBefore = observer.triggers();

  OAK_CHECK(attempt.engine.callWithLevel(Role::Set, 0.25));
  OAK_CHECK_EQ(observer.level(), 250);

  OAK_CHECK(attempt.engine.callWithName(Role::Post, "ice_drop"));
  OAK_CHECK_EQ(observer.triggers(), triggersBefore + 1);
  OAK_CHECK_EQ(observer.lastName(), std::string("ice_drop"));

  // A second call really is a second call, not a cached result.
  OAK_CHECK(attempt.engine.callWithLevel(Role::Set, 0.82));
  OAK_CHECK_EQ(observer.level(), 820);
}

OAK_TEST(prober_refuses_a_shape_the_role_did_not_declare) {
  const FixtureTree tree;
  if (!tree.usable()) return;

  const auto scan = scanForCandidates({tree.path().u8string()});
  const BindingAttempt attempt = bindFirstAvailable(scan.candidates, fixtureProfiles());
  OAK_CHECK(attempt.engine.isBound());

  // SET is bound with a level shape, POST with a name shape. Asking for the
  // other one must fail rather than call anything: the arguments would not fit.
  OAK_CHECK(!attempt.engine.callWithName(Role::Set, "wrong"));
  OAK_CHECK(!attempt.engine.callWithLevel(Role::Post, 0.5));
}

OAK_TEST(prober_reports_no_binding_when_a_symbol_is_absent) {
  const FixtureTree tree;
  if (!tree.usable()) return;

  const auto scan = scanForCandidates({tree.path().u8string()});
  const BindingAttempt attempt = bindFirstAvailable(scan.candidates, missProfiles());

  OAK_CHECK(!attempt.engine.isBound());
  // The module loaded; it just did not match. Those are different facts, and
  // reporting them separately is what makes a diagnosis possible.
  OAK_CHECK_EQ(attempt.candidatesLoaded, 1u);
  OAK_CHECK_EQ(attempt.profilesTried, 1u);
}

OAK_TEST(prober_reports_no_binding_when_there_are_no_profiles) {
  const FixtureTree tree;
  if (!tree.usable()) return;

  const auto scan = scanForCandidates({tree.path().u8string()});
  const BindingAttempt attempt = bindFirstAvailable(scan.candidates, {});
  OAK_CHECK(!attempt.engine.isBound());
  // Nothing was even loaded: with no description of what to look for, opening
  // somebody's module would be work done for no reason.
  OAK_CHECK_EQ(attempt.candidatesLoaded, 0u);
}

OAK_TEST(prober_reports_no_binding_when_there_are_no_candidates) {
  const BindingAttempt attempt = bindFirstAvailable({}, fixtureProfiles());
  OAK_CHECK(!attempt.engine.isBound());
  OAK_CHECK_EQ(attempt.candidatesLoaded, 0u);
}

OAK_TEST(dynamic_library_resolves_and_reports_absent_names) {
  const FixtureTree tree;
  if (!tree.usable()) return;

  Reason reason = Reason::Ok;
  const DynamicLibrary library = DynamicLibrary::load(tree.modulePath().u8string(), reason);
  OAK_CHECK(library.isLoaded());
  OAK_CHECK(reason == Reason::Ok);

  OAK_CHECK(library.resolve("oak_fixture_trigger") != nullptr);
  OAK_CHECK(library.resolve("no_such_symbol_at_all") == nullptr);
  OAK_CHECK(library.resolve("") == nullptr);
}

OAK_TEST(dynamic_library_fails_cleanly_on_something_that_is_not_a_module) {
  Reason reason = Reason::Ok;
  const DynamicLibrary missing = DynamicLibrary::load("no/such/module/anywhere", reason);
  OAK_CHECK(!missing.isLoaded());
  OAK_CHECK(reason == Reason::BindFailed);

  // An empty path is the caller's mistake, not the platform's, and the reason
  // code says so rather than blaming a load.
  Reason emptyReason = Reason::Ok;
  const DynamicLibrary empty = DynamicLibrary::load("", emptyReason);
  OAK_CHECK(!empty.isLoaded());
  OAK_CHECK(emptyReason == Reason::BadArgument);
}

OAK_TEST(dynamic_library_moves_without_double_freeing) {
  const FixtureTree tree;
  if (!tree.usable()) return;

  Reason reason = Reason::Ok;
  DynamicLibrary first = DynamicLibrary::load(tree.modulePath().u8string(), reason);
  OAK_CHECK(first.isLoaded());

  DynamicLibrary second = std::move(first);
  OAK_CHECK(second.isLoaded());
  // The moved-from library must be empty, or its destructor would unload a
  // module the new owner is still using.
  OAK_CHECK(!first.isLoaded());
  OAK_CHECK(first.resolve("oak_fixture_trigger") == nullptr);

  // Still usable through the new owner, and unloading twice is not attempted.
  OAK_CHECK(second.resolve("oak_fixture_trigger") != nullptr);
}
