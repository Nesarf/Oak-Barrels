#include "harness.hpp"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "relay/discovery/module_format.hpp"

namespace {

using oak::relay::discovery::identifyModuleFormat;
using oak::relay::discovery::isLoadableModuleFormat;
using oak::relay::discovery::kModuleHeaderBytes;
using oak::relay::discovery::ModuleFormat;
using oak::relay::discovery::moduleFormatName;

std::vector<std::uint8_t> bytes(std::initializer_list<std::uint8_t> values) {
  return std::vector<std::uint8_t>(values);
}

/// A minimal but well-formed PE header: the DOS magic, and a PE signature at
/// the offset the DOS header points to.
std::vector<std::uint8_t> peHeader() {
  std::vector<std::uint8_t> header(0x48, 0);
  header[0] = 'M';
  header[1] = 'Z';
  header[0x3c] = 0x40;
  header[0x40] = 'P';
  header[0x41] = 'E';
  return header;
}

}  // namespace

OAK_TEST(module_format_identifies_elf) {
  const auto header = bytes({0x7f, 'E', 'L', 'F', 2, 1, 1, 0});
  OAK_CHECK(identifyModuleFormat(header) == ModuleFormat::Elf);
  OAK_CHECK(isLoadableModuleFormat(ModuleFormat::Elf));
}

OAK_TEST(module_format_identifies_portable_executable) {
  const auto header = peHeader();
  OAK_CHECK(identifyModuleFormat(header) == ModuleFormat::PortableExecutable);
  OAK_CHECK(isLoadableModuleFormat(ModuleFormat::PortableExecutable));
}

OAK_TEST(module_format_identifies_mach_o) {
  OAK_CHECK(identifyModuleFormat(bytes({0xfe, 0xed, 0xfa, 0xce})) == ModuleFormat::MachO);
  OAK_CHECK(identifyModuleFormat(bytes({0xfe, 0xed, 0xfa, 0xcf})) == ModuleFormat::MachO);
  OAK_CHECK(identifyModuleFormat(bytes({0xce, 0xfa, 0xed, 0xfe})) == ModuleFormat::MachO);
  OAK_CHECK(identifyModuleFormat(bytes({0xca, 0xfe, 0xba, 0xbe})) == ModuleFormat::MachO);
}

OAK_TEST(module_format_rejects_what_is_not_a_module) {
  OAK_CHECK(!isLoadableModuleFormat(identifyModuleFormat(bytes({'h', 'e', 'l', 'l', 'o'}))));
  OAK_CHECK(!isLoadableModuleFormat(identifyModuleFormat(bytes({0, 0, 0, 0}))));
  OAK_CHECK(!isLoadableModuleFormat(identifyModuleFormat(bytes({0xff, 0xff, 0xff, 0xff}))));
  OAK_CHECK(identifyModuleFormat(bytes({'h', 'e', 'l', 'l', 'o'})) == ModuleFormat::Unknown);
}

OAK_TEST(module_format_rejects_input_that_is_too_short) {
  OAK_CHECK(identifyModuleFormat(bytes({})) == ModuleFormat::Unknown);
  OAK_CHECK(identifyModuleFormat(bytes({0x7f})) == ModuleFormat::Unknown);
  OAK_CHECK(identifyModuleFormat(bytes({0x7f, 'E', 'L'})) == ModuleFormat::Unknown);
}

OAK_TEST(module_format_tolerates_a_null_header) {
  OAK_CHECK(identifyModuleFormat(nullptr, 0) == ModuleFormat::Unknown);
  OAK_CHECK(identifyModuleFormat(nullptr, 64) == ModuleFormat::Unknown);
}

OAK_TEST(module_format_does_not_guess_at_a_bare_dos_header) {
  // "MZ" alone is not enough: plenty of things begin with it. Guessing here is
  // how a relay starts claiming things it cannot back up.
  std::vector<std::uint8_t> header(0x48, 0);
  header[0] = 'M';
  header[1] = 'Z';
  OAK_CHECK(identifyModuleFormat(header) == ModuleFormat::Unknown);

  // A signature pointer running past the bytes we were handed is also not
  // enough, for the same reason.
  std::vector<std::uint8_t> beyond(0x48, 0);
  beyond[0] = 'M';
  beyond[1] = 'Z';
  beyond[0x3c] = 0xff;
  OAK_CHECK(identifyModuleFormat(beyond) == ModuleFormat::Unknown);
}

OAK_TEST(module_format_ignores_bytes_after_the_magic) {
  const auto header = bytes({0x7f, 'E', 'L', 'F', 2, 1, 1, 0, 0xde, 0xad, 0xbe, 0xef});
  OAK_CHECK(identifyModuleFormat(header) == ModuleFormat::Elf);
}

OAK_TEST(module_format_header_window_covers_a_pe_signature) {
  // The PE signature is reached through a pointer, so the window has to be
  // wider than the magic numbers alone would need.
  OAK_CHECK(kModuleHeaderBytes >= 0x44);
}

OAK_TEST(module_format_names_are_short_and_neutral) {
  OAK_CHECK_EQ(std::string(moduleFormatName(ModuleFormat::Unknown)), std::string("unknown"));
  OAK_CHECK_EQ(std::string(moduleFormatName(ModuleFormat::Elf)), std::string("elf"));
  OAK_CHECK_EQ(std::string(moduleFormatName(ModuleFormat::MachO)), std::string("mach-o"));
  OAK_CHECK_EQ(std::string(moduleFormatName(ModuleFormat::PortableExecutable)),
               std::string("portable-executable"));
}

OAK_TEST(module_format_unknown_is_the_only_unloadable_one) {
  OAK_CHECK(!isLoadableModuleFormat(ModuleFormat::Unknown));
  OAK_CHECK(isLoadableModuleFormat(ModuleFormat::Elf));
  OAK_CHECK(isLoadableModuleFormat(ModuleFormat::MachO));
  OAK_CHECK(isLoadableModuleFormat(ModuleFormat::PortableExecutable));
}
