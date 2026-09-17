#include "relay/transport/named_pipe_transport.hpp"

#ifdef _WIN32

#include <algorithm>
#include <string>
#include <windows.h>

#include "relay/platform/wide_string.hpp"

namespace oak::relay::transport {
namespace {

const std::wstring kPipePrefix = L"\\\\.\\pipe\\";

bool hasPipePrefix(const std::wstring& name) {
  return name.size() >= kPipePrefix.size() &&
         name.compare(0, kPipePrefix.size(), kPipePrefix) == 0;
}

}  // namespace

NamedPipeTransport::~NamedPipeTransport() {
  if (pipe_ == nullptr) return;

  ::FlushFileBuffers(static_cast<HANDLE>(pipe_));
  ::DisconnectNamedPipe(static_cast<HANDLE>(pipe_));
  ::CloseHandle(static_cast<HANDLE>(pipe_));
  pipe_ = nullptr;
}

std::unique_ptr<NamedPipeTransport> NamedPipeTransport::prepare(const std::string& name,
                                                                std::string& error) {
  if (name.empty()) {
    error = "a pipe name is required";
    return nullptr;
  }

  std::wstring wide = oak::relay::platform::toWide(name);
  if (wide.empty()) {
    error = "the pipe name could not be used on this platform";
    return nullptr;
  }
  if (!hasPipePrefix(wide)) {
    wide = kPipePrefix + wide;
  }

  // One instance, and remote clients rejected outright. A second local client
  // is refused by the operating system rather than queued by us (section 2),
  // and this pipe is never a network surface.
  HANDLE pipe = ::CreateNamedPipeW(
      wide.c_str(), PIPE_ACCESS_DUPLEX,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
      static_cast<DWORD>(kMaxIoChunk), static_cast<DWORD>(kMaxIoChunk), 0, nullptr);
  if (pipe == INVALID_HANDLE_VALUE) {
    error = "could not create the named pipe";
    return nullptr;
  }

  std::unique_ptr<NamedPipeTransport> transport(new NamedPipeTransport());
  transport->pipe_ = static_cast<void*>(pipe);
  error.clear();
  return transport;
}

bool NamedPipeTransport::acceptOne(std::string& error) {
  if (pipe_ == nullptr) {
    error = "no pipe";
    return false;
  }

  const BOOL connected = ::ConnectNamedPipe(static_cast<HANDLE>(pipe_), nullptr);
  // ERROR_PIPE_CONNECTED means a client arrived between creation and this call,
  // which is a success that happens to be reported as a failure.
  if (connected != FALSE || ::GetLastError() == ERROR_PIPE_CONNECTED) {
    error.clear();
    return true;
  }

  error = "waiting for a client failed";
  return false;
}

std::ptrdiff_t NamedPipeTransport::read(std::uint8_t* buffer, std::size_t maxBytes) {
  if (pipe_ == nullptr || buffer == nullptr || maxBytes == 0) return 0;

  const DWORD chunk = static_cast<DWORD>(std::min(maxBytes, kMaxIoChunk));
  DWORD received = 0;

  if (::ReadFile(static_cast<HANDLE>(pipe_), buffer, chunk, &received, nullptr) != FALSE) {
    return static_cast<std::ptrdiff_t>(received);
  }

  // A client that hung up is a clean end of stream, not a failure.
  if (::GetLastError() == ERROR_BROKEN_PIPE) return 0;
  return -1;
}

bool NamedPipeTransport::write(const std::vector<std::uint8_t>& bytes) {
  if (pipe_ == nullptr) return false;

  std::size_t written = 0;
  while (written < bytes.size()) {
    const DWORD chunk = static_cast<DWORD>(std::min(bytes.size() - written, kMaxIoChunk));
    DWORD sent = 0;

    if (::WriteFile(static_cast<HANDLE>(pipe_), bytes.data() + written, chunk, &sent,
                    nullptr) == FALSE) {
      return false;
    }
    if (sent == 0) return false;

    written += static_cast<std::size_t>(sent);
  }

  return true;
}

}  // namespace oak::relay::transport

#else  // not this platform

namespace oak::relay::transport {

NamedPipeTransport::~NamedPipeTransport() = default;

std::unique_ptr<NamedPipeTransport> NamedPipeTransport::prepare(const std::string& /*name*/,
                                                                std::string& error) {
  error = "named pipes are not available on this platform";
  return nullptr;
}

bool NamedPipeTransport::acceptOne(std::string& error) {
  error = "named pipes are not available on this platform";
  return false;
}

std::ptrdiff_t NamedPipeTransport::read(std::uint8_t* /*buffer*/, std::size_t /*maxBytes*/) {
  return -1;
}

bool NamedPipeTransport::write(const std::vector<std::uint8_t>& /*bytes*/) { return false; }

}  // namespace oak::relay::transport

#endif
