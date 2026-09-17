#include "relay/transport/unix_socket_transport.hpp"

#if defined(__unix__) || defined(__APPLE__)

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace oak::relay::transport {
namespace {

bool isInterrupted(int result) { return result < 0 && errno == EINTR; }

/// Sends without raising SIGPIPE.
///
/// A relay that dies from a signal because a host hung up is a relay that
/// cannot report why it stopped.
int sendFlags() {
#ifdef MSG_NOSIGNAL
  return MSG_NOSIGNAL;
#else
  return 0;
#endif
}

}  // namespace

UnixSocketTransport::~UnixSocketTransport() {
  if (connectionFd_ >= 0) ::close(connectionFd_);
  if (listenFd_ >= 0) ::close(listenFd_);

  // The socket file is ours to remove: we created it, at a path the host named.
  if (!path_.empty()) ::unlink(path_.c_str());
}

std::unique_ptr<UnixSocketTransport> UnixSocketTransport::prepare(const std::string& path,
                                                                  std::string& error) {
  if (path.empty()) {
    error = "a socket path is required";
    return nullptr;
  }

  // A path longer than the platform can express cannot be bound, and truncating
  // it would bind somewhere the host did not ask for.
  if (path.size() >= sizeof(sockaddr_un::sun_path)) {
    error = "the socket path is too long for this platform";
    return nullptr;
  }

  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    error = "could not create a socket";
    return nullptr;
  }

  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, path.c_str(), path.size());

  if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
    // Deliberately no unlink-and-retry. The path was named by the host, and
    // clearing whatever occupies it is the host's call, not ours.
    error = "could not bind the socket path";
    ::close(fd);
    return nullptr;
  }

  if (::listen(fd, 1) < 0) {
    error = "could not listen on the socket";
    ::close(fd);
    ::unlink(path.c_str());
    return nullptr;
  }

  std::unique_ptr<UnixSocketTransport> transport(new UnixSocketTransport());
  transport->listenFd_ = fd;
  transport->path_ = path;
  error.clear();
  return transport;
}

bool UnixSocketTransport::acceptOne(std::string& error) {
  if (listenFd_ < 0) {
    error = "not listening";
    return false;
  }

  for (;;) {
    const int fd = ::accept(listenFd_, nullptr, nullptr);
    if (isInterrupted(fd)) continue;

    if (fd < 0) {
      error = "accepting the connection failed";
      return false;
    }

    connectionFd_ = fd;

    // One connection, and then no more. Closing the listener now means a second
    // client is refused by the operating system rather than queued by us
    // (section 2).
    ::close(listenFd_);
    listenFd_ = -1;

    error.clear();
    return true;
  }
}

std::ptrdiff_t UnixSocketTransport::read(std::uint8_t* buffer, std::size_t maxBytes) {
  if (connectionFd_ < 0 || buffer == nullptr || maxBytes == 0) return 0;

  const std::size_t chunk = std::min(maxBytes, kMaxIoChunk);

  for (;;) {
    const ssize_t result = ::recv(connectionFd_, buffer, chunk, 0);
    if (isInterrupted(static_cast<int>(result))) continue;
    return static_cast<std::ptrdiff_t>(result);
  }
}

bool UnixSocketTransport::write(const std::vector<std::uint8_t>& bytes) {
  if (connectionFd_ < 0) return false;

  std::size_t written = 0;
  while (written < bytes.size()) {
    const std::size_t chunk = std::min(bytes.size() - written, kMaxIoChunk);

    const ssize_t result =
        ::send(connectionFd_, bytes.data() + written, chunk, sendFlags());
    if (isInterrupted(static_cast<int>(result))) continue;
    if (result <= 0) return false;

    written += static_cast<std::size_t>(result);
  }

  return true;
}

}  // namespace oak::relay::transport

#else  // not a unix-like platform

namespace oak::relay::transport {

UnixSocketTransport::~UnixSocketTransport() = default;

std::unique_ptr<UnixSocketTransport> UnixSocketTransport::prepare(const std::string& /*path*/,
                                                                  std::string& error) {
  error = "unix domain sockets are not available on this platform";
  return nullptr;
}

bool UnixSocketTransport::acceptOne(std::string& error) {
  error = "unix domain sockets are not available on this platform";
  return false;
}

std::ptrdiff_t UnixSocketTransport::read(std::uint8_t* /*buffer*/, std::size_t /*maxBytes*/) {
  return -1;
}

bool UnixSocketTransport::write(const std::vector<std::uint8_t>& /*bytes*/) { return false; }

}  // namespace oak::relay::transport

#endif
