#include "relay/discovery/module_format.hpp"

namespace oak::relay::discovery {
namespace {

/// Offset of the DOS header field that points at the PE signature.
constexpr std::size_t kPeSignaturePointerOffset = 0x3c;
constexpr std::size_t kPeSignatureLength = 4;

std::uint32_t readLittleEndian32(const std::uint8_t* bytes) {
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8) |
         (static_cast<std::uint32_t>(bytes[2]) << 16) |
         (static_cast<std::uint32_t>(bytes[3]) << 24);
}

std::uint32_t readBigEndian32(const std::uint8_t* bytes) {
  return (static_cast<std::uint32_t>(bytes[0]) << 24) |
         (static_cast<std::uint32_t>(bytes[1]) << 16) |
         (static_cast<std::uint32_t>(bytes[2]) << 8) |
         static_cast<std::uint32_t>(bytes[3]);
}

bool startsWith(const std::uint8_t* data,
                std::size_t length,
                const char* magic,
                std::size_t magicLength) {
  if (length < magicLength) return false;
  for (std::size_t i = 0; i < magicLength; ++i) {
    if (data[i] != static_cast<std::uint8_t>(magic[i])) return false;
  }
  return true;
}

bool looksLikePortableExecutable(const std::uint8_t* data, std::size_t length) {
  if (length < kPeSignaturePointerOffset + 4) return false;
  if (data[0] != static_cast<std::uint8_t>('M')) return false;
  if (data[1] != static_cast<std::uint8_t>('Z')) return false;

  const std::uint32_t signatureOffset = readLittleEndian32(data + kPeSignaturePointerOffset);
  if (signatureOffset + kPeSignatureLength > length) return false;

  return startsWith(data + signatureOffset, length - signatureOffset, "PE\0\0",
                    kPeSignatureLength);
}

bool looksLikeElf(const std::uint8_t* data, std::size_t length) {
  return startsWith(data, length, "\x7f" "ELF", 4);
}

bool looksLikeMachO(const std::uint8_t* data, std::size_t length) {
  if (length < 4) return false;

  switch (readBigEndian32(data)) {
    case 0xfeedfaceu:  // 32-bit
    case 0xfeedfacfu:  // 64-bit
    case 0xcefaedfeu:  // 32-bit, byte-swapped
    case 0xcffaedfeu:  // 64-bit, byte-swapped
    case 0xcafebabeu:  // universal
    case 0xcafebabfu:  // universal, 64-bit
      return true;
    default:
      return false;
  }
}

}  // namespace

const char* moduleFormatName(ModuleFormat format) {
  switch (format) {
    case ModuleFormat::Unknown:
      return "unknown";
    case ModuleFormat::PortableExecutable:
      return "portable-executable";
    case ModuleFormat::Elf:
      return "elf";
    case ModuleFormat::MachO:
      return "mach-o";
  }
  return "unknown";
}

bool isLoadableModuleFormat(ModuleFormat format) {
  return format != ModuleFormat::Unknown;
}

ModuleFormat identifyModuleFormat(const std::uint8_t* header, std::size_t length) {
  if (header == nullptr || length < 4) return ModuleFormat::Unknown;

  // Order is arbitrary but must be deterministic. None of these magics can be
  // mistaken for another, with one caveat noted below.
  if (looksLikeElf(header, length)) return ModuleFormat::Elf;
  if (looksLikeMachO(header, length)) return ModuleFormat::MachO;
  if (looksLikePortableExecutable(header, length)) return ModuleFormat::PortableExecutable;

  return ModuleFormat::Unknown;
}

ModuleFormat identifyModuleFormat(const std::vector<std::uint8_t>& header) {
  return identifyModuleFormat(header.data(), header.size());
}

// One honest ambiguity, recorded here rather than left for someone to discover:
// the universal Mach-O magic 0xCAFEBABE is also the magic of a compiled Java
// class file. Distinguishing them needs the field after the magic, which is a
// language-runtime question this layer has no business answering.
//
// The consequence is bounded and acceptable. A format never yields a
// compatibility class on its own (see the class mapping in this directory), so
// a misidentified file means only that the relay reads a few more bytes of a
// file it will then decline to claim anything about. The relay over-examines;
// it never over-claims.

}  // namespace oak::relay::discovery
