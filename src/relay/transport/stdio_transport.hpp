// Byte transport over standard input and standard output.
//
// This is the transport the relay station uses when a host process launches
// it as a child: the host writes requests to the relay's stdin and reads
// replies from its stdout. No socket is opened and no port is bound, which is
// why the default deployment needs no firewall consideration at all.
//
// Standing rule for every transport in this tree: **stdout carries frames and
// nothing else.** Diagnostics go to stderr. A stray line on stdout desynchronises
// the frame stream, and the peer has no way to tell the noise from a header.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "relay/transport/transport.hpp"

namespace oak::relay::transport {

/// Blocking byte transport over the process's standard streams.
class StdioTransport final : public Transport {
 public:
  StdioTransport();
  ~StdioTransport() override;

  StdioTransport(const StdioTransport&) = delete;
  StdioTransport& operator=(const StdioTransport&) = delete;

  /// Reads up to `maxBytes` into `buffer`.
  ///
  /// Returns the number of bytes read (which may be 0 at end of stream), or a
  /// negative value on error. Interrupted reads are retried internally.
  std::ptrdiff_t read(std::uint8_t* buffer, std::size_t maxBytes) override;

  /// Writes every byte of `bytes`. Returns false on error or short write.
  bool write(const std::vector<std::uint8_t>& bytes) override;
};

}  // namespace oak::relay::transport
