// A named pipe, serving exactly one client.
//
// The name is chosen by the host, never by the relay. Protocol section 2 is
// explicit about why: a name the relay invents is a name anything else on the
// machine can guess and connect to.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "relay/transport/transport.hpp"

namespace oak::relay::transport {

class NamedPipeTransport final : public Transport {
 public:
  ~NamedPipeTransport() override;

  NamedPipeTransport(const NamedPipeTransport&) = delete;
  NamedPipeTransport& operator=(const NamedPipeTransport&) = delete;

  /// Creates the pipe under [name]. Returns nullptr and sets [error] on failure.
  ///
  /// [name] may be given either in full or as the bare name; the platform's own
  /// prefix is added when it is missing. Adding a prefix the operating system
  /// requires is translating, not inventing: the part that identifies this pipe
  /// still comes from the host.
  static std::unique_ptr<NamedPipeTransport> prepare(const std::string& name,
                                                     std::string& error);

  /// Waits for the one client this transport will ever serve.
  ///
  /// The pipe is created with a single instance, so a second client is refused
  /// by the operating system rather than queued by us (section 2).
  bool acceptOne(std::string& error);

  std::ptrdiff_t read(std::uint8_t* buffer, std::size_t maxBytes) override;
  bool write(const std::vector<std::uint8_t>& bytes) override;

 private:
  NamedPipeTransport() = default;

  void* pipe_ = nullptr;  // A HANDLE, kept opaque so this header stays portable.
};

}  // namespace oak::relay::transport
