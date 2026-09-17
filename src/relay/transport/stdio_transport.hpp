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

namespace oak::relay::transport {

/// Blocking byte transport over the process's standard streams.
///
/// Reads return as soon as *any* bytes are available rather than waiting to
/// fill the caller's buffer -- a frame stream is not a file, and a read that
/// waits for a full buffer would deadlock against a peer that is waiting for
/// a reply.
class StdioTransport {
 public:
  StdioTransport();
  ~StdioTransport();

  StdioTransport(const StdioTransport&) = delete;
  StdioTransport& operator=(const StdioTransport&) = delete;

  /// Reads up to `maxBytes` into `buffer`.
  ///
  /// Returns the number of bytes read (which may be 0 at end of stream), or a
  /// negative value on error. Interrupted reads are retried internally.
  std::ptrdiff_t read(std::uint8_t* buffer, std::size_t maxBytes);

  /// Writes every byte of `bytes`. Returns false on error or short write.
  bool write(const std::vector<std::uint8_t>& bytes);

  /// Largest single read or write this transport will issue.
  static constexpr std::size_t kMaxIoChunk = 64u * 1024u;
};

}  // namespace oak::relay::transport
