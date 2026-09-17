// Transports, exercised against a real client.
//
// A transport that has never carried a byte has not been tested. Both cases
// below connect *before* accepting: the listener is already up, so the client
// side cannot block, and by the time accept runs there is a connection waiting
// for it. That ordering is what keeps this file free of threads and free of any
// way to hang a build.
//
// Only the transport this platform actually provides can be exercised. The
// other one is asserted to refuse cleanly, because "not available here" is a
// behaviour worth pinning down rather than an absence worth ignoring.

#include "harness.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "relay/transport/named_pipe_transport.hpp"
#include "relay/transport/unix_socket_transport.hpp"

#if defined(_WIN32)
#include <windows.h>

#include "relay/platform/wide_string.hpp"
#else
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace {

namespace fs = std::filesystem;

using oak::relay::transport::NamedPipeTransport;
using oak::relay::transport::UnixSocketTransport;

/// True when the fixture path is short enough for the platform to express.
///
/// Only the socket tests need this, so it is unused on the platform that has no
/// sockets to test.
[[maybe_unused]] bool pathIsUsable(const std::string& path) {
#if defined(_WIN32)
  (void)path;
  return true;
#else
  return path.size() < sizeof(sockaddr_un::sun_path);
#endif
}

std::string uniqueSocketPath(const char* tag) {
  static int counter = 0;
  std::error_code error;
  const fs::path base = fs::temp_directory_path(error);
  return (base / (std::string("oak-transport-") + tag + "-" + std::to_string(counter++) + ".sock"))
      .u8string();
}

std::string uniquePipeName(const char* tag) {
  static int counter = 0;
  return std::string("oak-transport-") + tag + "-" + std::to_string(counter++);
}

}  // namespace

#if defined(_WIN32)

OAK_TEST(named_pipe_carries_bytes_both_ways) {
  const std::string name = uniquePipeName("round-trip");

  std::string error;
  auto server = NamedPipeTransport::prepare(name, error);
  OAK_CHECK(server != nullptr);

  // The bare name is what the host supplies; the platform prefix is added for
  // it, so the client builds the same full name here.
  const std::wstring full = L"\\\\.\\pipe\\" + oak::relay::platform::toWide(name);
  HANDLE client = ::CreateFileW(full.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                OPEN_EXISTING, 0, nullptr);
  OAK_CHECK(client != INVALID_HANDLE_VALUE);

  // A client is already waiting, so this returns at once.
  OAK_CHECK(server->acceptOne(error));

  const std::string message = "hello from the host";
  DWORD sent = 0;
  OAK_CHECK(::WriteFile(client, message.data(), static_cast<DWORD>(message.size()), &sent,
                        nullptr) != FALSE);
  OAK_CHECK_EQ(sent, static_cast<DWORD>(message.size()));

  std::uint8_t buffer[64] = {};
  const std::ptrdiff_t got = server->read(buffer, sizeof(buffer));
  OAK_CHECK(got > 0);
  OAK_CHECK_EQ(std::string(reinterpret_cast<char*>(buffer), static_cast<std::size_t>(got)),
               message);

  const std::string reply = "and back";
  OAK_CHECK(server->write(std::vector<std::uint8_t>(reply.begin(), reply.end())));

  DWORD received = 0;
  OAK_CHECK(::ReadFile(client, buffer, sizeof(buffer), &received, nullptr) != FALSE);
  OAK_CHECK_EQ(std::string(reinterpret_cast<char*>(buffer), static_cast<std::size_t>(received)),
               reply);

  ::CloseHandle(client);
}

OAK_TEST(named_pipe_refuses_a_name_it_cannot_use) {
  std::string error;
  OAK_CHECK(NamedPipeTransport::prepare("", error) == nullptr);
  OAK_CHECK(!error.empty());
}

OAK_TEST(unix_sockets_are_unavailable_here) {
  std::string error;
  OAK_CHECK(UnixSocketTransport::prepare(uniqueSocketPath("unavailable"), error) == nullptr);
  OAK_CHECK(error.find("not available") != std::string::npos);
}

#else  // a unix-like platform

OAK_TEST(unix_socket_carries_bytes_both_ways) {
  const std::string path = uniqueSocketPath("round-trip");
  if (!pathIsUsable(path)) return;  // the temporary directory is too deep here

  ::unlink(path.c_str());

  std::string error;
  auto server = UnixSocketTransport::prepare(path, error);
  OAK_CHECK(server != nullptr);

  // Connect first: the listener is already up, so this cannot block, and the
  // accept below finds a connection waiting instead of the test waiting on
  // accept.
  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  OAK_CHECK(fd >= 0);

  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, path.c_str(), path.size());
  OAK_CHECK_EQ(::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);

  OAK_CHECK(server->acceptOne(error));

  const std::string message = "hello from the host";
  OAK_CHECK_EQ(::send(fd, message.data(), message.size(), 0),
               static_cast<ssize_t>(message.size()));

  std::uint8_t buffer[64] = {};
  const std::ptrdiff_t got = server->read(buffer, sizeof(buffer));
  OAK_CHECK(got > 0);
  OAK_CHECK_EQ(std::string(reinterpret_cast<char*>(buffer), static_cast<std::size_t>(got)),
               message);

  const std::string reply = "and back";
  OAK_CHECK(server->write(std::vector<std::uint8_t>(reply.begin(), reply.end())));

  const ssize_t back = ::recv(fd, buffer, sizeof(buffer), 0);
  OAK_CHECK(back > 0);
  OAK_CHECK_EQ(std::string(reinterpret_cast<char*>(buffer), static_cast<std::size_t>(back)),
               reply);

  ::close(fd);
  ::unlink(path.c_str());
}

OAK_TEST(unix_socket_refuses_to_clobber_a_path_in_use) {
  const std::string path = uniqueSocketPath("in-use");
  if (!pathIsUsable(path)) return;

  ::unlink(path.c_str());

  std::string firstError;
  auto first = UnixSocketTransport::prepare(path, firstError);
  OAK_CHECK(first != nullptr);

  // Binding again must fail rather than remove what is already there. The path
  // was named by the host, and clearing it is the host's call.
  std::string secondError;
  OAK_CHECK(UnixSocketTransport::prepare(path, secondError) == nullptr);
  OAK_CHECK(!secondError.empty());

  first.reset();
  ::unlink(path.c_str());
}

OAK_TEST(unix_socket_refuses_a_path_it_cannot_express) {
  std::string error;
  OAK_CHECK(UnixSocketTransport::prepare("", error) == nullptr);

  // Longer than any platform can put in a socket address. Truncating it would
  // bind somewhere the host did not ask for.
  const std::string tooLong(512, 'x');
  std::string longError;
  OAK_CHECK(UnixSocketTransport::prepare(tooLong, longError) == nullptr);
}

OAK_TEST(named_pipes_are_unavailable_here) {
  std::string error;
  OAK_CHECK(NamedPipeTransport::prepare(uniquePipeName("unavailable"), error) == nullptr);
  OAK_CHECK(error.find("not available") != std::string::npos);
}

#endif
