// A stand-in engine, used to prove the discover -> probe -> bind -> call path
// end to end.
//
// It is deliberately not a real engine, and it deliberately lives in the test
// tree rather than in src/. The point is to exercise symbol resolution and the
// call path without the relay shipping any knowledge of a particular product:
// the symbol names below are ours, and a profile names them at run time, from a
// file the host supplied.
//
// It records what it was called with so that a test can check the call actually
// landed, rather than only that it did not fail.

#include <cstddef>
#include <cstdint>

namespace {

std::int64_t g_lastLevel = -1;
std::int64_t g_triggerCount = 0;
char g_lastName[64] = {0};

}  // namespace

#if defined(_WIN32)
#define OAK_FIXTURE_EXPORT extern "C" __declspec(dllexport)
#else
#define OAK_FIXTURE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

/// Takes a continuous value. Bound to the SET role by the test profile.
OAK_FIXTURE_EXPORT void oak_fixture_set_level(double value) {
  g_lastLevel = static_cast<std::int64_t>(value * 1000.0);
}

/// Takes a name. Bound to the POST role by the test profile.
OAK_FIXTURE_EXPORT void oak_fixture_trigger(const char* name) {
  ++g_triggerCount;

  if (name == nullptr) {
    g_lastName[0] = '\0';
    return;
  }

  // Copied by hand rather than with the CRT's bounded copy, which is deprecated
  // precisely because its truncation behaviour surprises people. This one
  // always terminates.
  std::size_t written = 0;
  while (written + 1 < sizeof(g_lastName) && name[written] != '\0') {
    g_lastName[written] = name[written];
    ++written;
  }
  g_lastName[written] = '\0';
}

/// Observation points, so a test can see what the relay actually did.
OAK_FIXTURE_EXPORT std::int64_t oak_fixture_level(void) { return g_lastLevel; }
OAK_FIXTURE_EXPORT std::int64_t oak_fixture_triggers(void) { return g_triggerCount; }
OAK_FIXTURE_EXPORT const char* oak_fixture_last_name(void) { return g_lastName; }
