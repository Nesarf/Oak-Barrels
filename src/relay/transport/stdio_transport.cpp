#include "relay/transport/stdio_transport.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace oak::relay::transport {
namespace {

constexpr int kStdinFd = 0;
constexpr int kStdoutFd = 1;

bool isInterrupted(int result) { return result < 0 && errno == EINTR; }

}  // namespace

StdioTransport::StdioTransport() {
#ifdef _WIN32
  // Without this, the CRT translates \n to \r\n on the way out and treats
  // 0x1a as end-of-file on the way in -- either one corrupts a frame stream.
  ::_setmode(::_fileno(stdin), _O_BINARY);
  ::_setmode(::_fileno(stdout), _O_BINARY);
#endif
}

StdioTransport::~StdioTransport() = default;

std::ptrdiff_t StdioTransport::read(std::uint8_t* buffer, std::size_t maxBytes) {
  if (buffer == nullptr || maxBytes == 0) return 0;

  const std::size_t chunk = std::min(maxBytes, kMaxIoChunk);

  for (;;) {
#ifdef _WIN32
    const int result = ::_read(kStdinFd, buffer, static_cast<unsigned int>(chunk));
#else
    const ssize_t result = ::read(kStdinFd, buffer, chunk);
#endif
    if (isInterrupted(static_cast<int>(result))) continue;
    return static_cast<std::ptrdiff_t>(result);
  }
}

bool StdioTransport::write(const std::vector<std::uint8_t>& bytes) {
  std::size_t written = 0;

  while (written < bytes.size()) {
    const std::size_t chunk = std::min(bytes.size() - written, kMaxIoChunk);

#ifdef _WIN32
    const int result = ::_write(kStdoutFd, bytes.data() + written, static_cast<unsigned int>(chunk));
#else
    const ssize_t result = ::write(kStdoutFd, bytes.data() + written, chunk);
#endif
    if (isInterrupted(static_cast<int>(result))) continue;
    if (result <= 0) return false;

    written += static_cast<std::size_t>(result);
  }

  return true;
}

}  // namespace oak::relay::transport
