// Mapping a module into this process, and resolving names inside it.
//
// This is the "bind" stage of the lifecycle. It does exactly two things:
// map a module, and look up a symbol. It calls nothing.
//
// That restraint is deliberate. Resolving a symbol establishes that a name
// exists; it says nothing about what happens when it is called. Whoever decides
// to call one needs a shape to call it with, and this layer is not the place
// that decision belongs -- see src/relay/probe/ for where it is made, and for
// why the shapes there are described by the host rather than by this build.
#pragma once

#include <string>

#include "relay/protocol/messages.hpp"

namespace oak::relay::binding {

/// A module mapped into this process.
///
/// The library remembers nothing about where it came from. No path is stored,
/// logged, or reported: per axiom A4 a discovered path stays where it was found.
class DynamicLibrary {
 public:
  DynamicLibrary() = default;
  ~DynamicLibrary();

  DynamicLibrary(const DynamicLibrary&) = delete;
  DynamicLibrary& operator=(const DynamicLibrary&) = delete;
  DynamicLibrary(DynamicLibrary&& other) noexcept;
  DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;

  /// Maps the module at [path].
  ///
  /// Returns an unloaded library and sets [reason] on failure. The reason codes
  /// are the protocol's, and none of them names the path or the platform's own
  /// error text -- a host learns *that* it failed, not *what* was tried.
  static DynamicLibrary load(const std::string& path, protocol::Reason& reason);

  bool isLoaded() const { return handle_ != nullptr; }
  explicit operator bool() const { return isLoaded(); }

  /// Resolves [symbol]. Returns nullptr when the name is not exported.
  ///
  /// A name that is absent is not an error at this layer: probing asks for
  /// names precisely to find out which are there.
  void* resolve(const std::string& symbol) const;

 private:
  void release();

  void* handle_ = nullptr;
};

}  // namespace oak::relay::binding
