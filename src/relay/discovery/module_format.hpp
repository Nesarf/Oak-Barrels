// Identifying what a file is, without learning where it came from.
//
// Discovery needs a neutral signal: something that says "this file is worth
// looking at" without naming a product, a vendor or a version. The operating
// system's own executable container formats are exactly that. PE, ELF and
// Mach-O are public, stable, and owned by nobody in particular, so recognising
// one tells the relay what a file *is* while telling it nothing about what the
// file is *from*.
//
// Note what this deliberately cannot do: identify a package, a bundle, or a
// plugin by name. Anything that recognised a vendor's layout would be a
// build-time coupling wearing a run-time costume, which is what axiom A2
// forbids.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace oak::relay::discovery {

/// The container format of a loadable module.
enum class ModuleFormat {
  Unknown,
  PortableExecutable,
  Elf,
  MachO,
};

/// A short, non-identifying name for [format]. Safe to log.
const char* moduleFormatName(ModuleFormat format);

/// True when the platform can load code from a container of this format.
///
/// A file in an unrecognised container is not an error. It is a file the relay
/// simply does not examine, which is the conservative direction to fail in.
bool isLoadableModuleFormat(ModuleFormat format);

/// How many leading bytes `identifyModuleFormat` wants.
///
/// Larger than the magic numbers alone need, because the PE signature is
/// reached through a pointer stored at offset 0x3C. A file whose signature lies
/// beyond this window is reported as Unknown rather than guessed at.
inline constexpr std::size_t kModuleHeaderBytes = 1024;

/// Identifies the format from the leading bytes, or Unknown.
///
/// Passing fewer bytes than the file has is fine, and is the point: a caller
/// can classify a file without reading all of it, which matters when the file
/// is a few hundred megabytes of samples.
ModuleFormat identifyModuleFormat(const std::uint8_t* header, std::size_t length);
ModuleFormat identifyModuleFormat(const std::vector<std::uint8_t>& header);

}  // namespace oak::relay::discovery
