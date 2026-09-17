// A test harness small enough to read in one sitting.
//
// Registration happens as a side effect of a file-scope object, so adding a
// test means writing `OAK_TEST(name) { ... }` and nothing else -- no list to
// keep in sync, which is the usual way tests quietly stop running.
#pragma once

#include <exception>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace oak::test {

/// Thrown by a failed check, carrying where it happened.
class Failure : public std::exception {
 public:
  Failure(std::string file, int line, std::string expression)
      : message_(std::move(file) + ":" + std::to_string(line) + ": " +
                 std::move(expression)) {}

  const char* what() const noexcept override { return message_.c_str(); }

 private:
  std::string message_;
};

struct Case {
  std::string name;
  std::function<void()> body;
};

/// The single registry, shared across translation units by virtue of being a
/// function-local static in an inline function.
inline std::vector<Case>& registry() {
  static std::vector<Case> cases;
  return cases;
}

struct Registrar {
  Registrar(const char* name, std::function<void()> body) {
    registry().push_back(Case{name, std::move(body)});
  }
};

/// Runs every registered case. Returns a process exit code.
inline int runAll() {
  int failures = 0;

  for (const Case& testCase : registry()) {
    try {
      testCase.body();
    } catch (const Failure& failure) {
      ++failures;
      std::cerr << "FAIL " << testCase.name << "\n  " << failure.what() << "\n";
      continue;
    } catch (const std::exception& error) {
      ++failures;
      std::cerr << "FAIL " << testCase.name << "\n  unexpected exception: "
                << error.what() << "\n";
      continue;
    }

    std::cout << "ok   " << testCase.name << "\n";
  }

  const std::size_t total = registry().size();
  if (failures == 0) {
    std::cout << "\nall " << total << " cases passed\n";
    return 0;
  }

  std::cout << "\n" << failures << " of " << total << " cases failed\n";
  return 1;
}

}  // namespace oak::test

#define OAK_TEST(name)                                                        \
  static void name();                                                         \
  static const ::oak::test::Registrar oak_registrar_##name(#name, name);      \
  static void name()

#define OAK_CHECK(condition)                                                   \
  do {                                                                         \
    if (!(condition)) {                                                        \
      throw ::oak::test::Failure(__FILE__, __LINE__,                           \
                                 "check failed: " #condition);                 \
    }                                                                          \
  } while (false)

#define OAK_CHECK_EQ(actual, expected)                                         \
  do {                                                                         \
    if (!((actual) == (expected))) {                                           \
      throw ::oak::test::Failure(__FILE__, __LINE__,                           \
                                 "expected " #actual " == " #expected);        \
    }                                                                          \
  } while (false)
