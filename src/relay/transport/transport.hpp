// One session's byte channel.
//
// Three transports exist (protocol section 2): standard streams, a unix domain
// socket, and a named pipe. This interface is what lets the station choose one
// at startup without the session above it knowing which.
//
// Two rules hold for every implementation, and both come from the protocol
// rather than from taste:
//
//   * A transport serves exactly one host and then ends. Additional connections
//     are refused, never queued -- the relay has no protocol for a second
//     conversation and should not pretend otherwise.
//   * The relay never listens on a TCP port by default. Network-exposed audio
//     control is a decision the host makes explicitly, not one this project
//     makes on its behalf.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace oak::relay::transport {

/// Largest single read or write any transport will issue.
inline constexpr std::size_t kMaxIoChunk = 64u * 1024u;

class Transport {
 public:
  virtual ~Transport() = default;

  Transport(const Transport&) = delete;
  Transport& operator=(const Transport&) = delete;

  /// Reads up to [maxBytes], returning as soon as any bytes are available.
  ///
  /// A frame stream is not a file. A read that waited to fill its buffer would
  /// deadlock against a peer waiting for the reply that read is blocking.
  ///
  /// Returns the number of bytes read, 0 for a clean end of stream, or a
  /// negative value on error.
  virtual std::ptrdiff_t read(std::uint8_t* buffer, std::size_t maxBytes) = 0;

  /// Writes every byte of [bytes]. Returns false on error or a short write.
  virtual bool write(const std::vector<std::uint8_t>& bytes) = 0;

 protected:
  Transport() = default;
};

}  // namespace oak::relay::transport
