// A unix domain socket, serving exactly one connection.
//
// The path is chosen by the host, never by the relay. Protocol section 2 is
// explicit about why: a name the relay invents is a name anything else on the
// machine can guess and connect to, and a relay that can be talked to by
// strangers is not a relay, it is a service.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "relay/transport/transport.hpp"

namespace oak::relay::transport {

class UnixSocketTransport final : public Transport {
 public:
  ~UnixSocketTransport() override;

  UnixSocketTransport(const UnixSocketTransport&) = delete;
  UnixSocketTransport& operator=(const UnixSocketTransport&) = delete;

  /// Binds and listens at [path]. Returns nullptr and sets [error] on failure.
  ///
  /// A file already sitting at [path] is *not* removed to make room. The host
  /// named that path; deleting whatever happens to be there is not this relay's
  /// decision to make, and a stale socket is a problem the host can see and
  /// clear.
  static std::unique_ptr<UnixSocketTransport> prepare(const std::string& path,
                                                      std::string& error);

  /// Waits for the one connection this transport will ever accept.
  ///
  /// The listening socket is closed as soon as it has done its job, so a second
  /// client is refused by the operating system rather than queued by us.
  bool acceptOne(std::string& error);

  std::ptrdiff_t read(std::uint8_t* buffer, std::size_t maxBytes) override;
  bool write(const std::vector<std::uint8_t>& bytes) override;

 private:
  UnixSocketTransport() = default;

  int listenFd_ = -1;
  int connectionFd_ = -1;
  std::string path_;
};

}  // namespace oak::relay::transport
