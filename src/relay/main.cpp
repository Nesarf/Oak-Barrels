// The relay station.
//
// Runs a single session over the process's standard streams: a host spawns
// this binary, writes frames to its stdin, and reads replies from its stdout.
// No socket, no port, no filesystem artifact, and nothing written to disk.
//
// Exit codes:
//   0  clean end of session
//   1  the peer violated the protocol, or the streams failed
//   2  bad command line

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "relay/backend.hpp"
#include "relay/discovery/discovery_backend.hpp"
#include "relay/protocol/frame.hpp"
#include "relay/session.hpp"
#include "relay/transport/stdio_transport.hpp"

#ifndef OAK_BARRELS_VERSION
#define OAK_BARRELS_VERSION "unversioned"
#endif

namespace {

const char* kUsage =
    "oak-barrels - a neutral relay between a host and an installed audio engine.\n"
    "\n"
    "Usage:\n"
    "  oak-barrels [--search-root <dir>]... [--version] [--help]\n"
    "\n"
    "With no arguments it serves one session over standard input and standard\n"
    "output. Frames are length-prefixed; see docs/RELAY_PROTOCOL.md.\n"
    "\n"
    "  --search-root <dir>  nominate a directory for discovery. Repeatable.\n"
    "                       The station invents no locations of its own and\n"
    "                       looks nowhere unless asked, so with no\n"
    "                       --search-root it never touches the filesystem.\n"
    "\n"
    "Standard output carries frames and nothing else. Diagnostics go to\n"
    "standard error, so a host may parse stdout without filtering it.\n";

/// Writes session frames to a stdio transport.
class StdioSink final : public oak::relay::FrameSink {
 public:
  explicit StdioSink(oak::relay::transport::StdioTransport& transport)
      : transport_(transport) {}

  bool writeFrame(const std::vector<std::uint8_t>& bytes) override {
    return transport_.write(bytes);
  }

 private:
  oak::relay::transport::StdioTransport& transport_;
};

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> searchRoots;

  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];

    if (argument == "--help" || argument == "-h") {
      std::fputs(kUsage, stdout);
      return 0;
    }
    if (argument == "--version") {
      std::fprintf(stdout, "oak-barrels %s\n", OAK_BARRELS_VERSION);
      return 0;
    }
    if (argument == "--search-root") {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "oak-barrels: --search-root needs a directory\n");
        return 2;
      }
      searchRoots.emplace_back(argv[++i]);
      continue;
    }

    std::fprintf(stderr, "oak-barrels: unrecognised argument\n");
    return 2;
  }

  oak::relay::transport::StdioTransport transport;
  StdioSink sink(transport);

  // Discovery is host-directed and opt-in. With no nominated root, no
  // filesystem code runs at all -- which is the default posture, because a
  // relay that surveyed the machine unprompted would be doing the exact thing
  // this project exists to avoid.
  std::unique_ptr<oak::relay::EngineBackend> backend;
  if (searchRoots.empty()) {
    backend = std::make_unique<oak::relay::NullBackend>();
  } else {
    backend = std::make_unique<oak::relay::discovery::DiscoveryBackend>(
        std::move(searchRoots));
  }

  oak::relay::Session session(*backend, sink);

  oak::relay::protocol::FrameDecoder decoder;
  std::vector<std::uint8_t> buffer(oak::relay::transport::StdioTransport::kMaxIoChunk);

  bool running = true;
  while (running) {
    const std::ptrdiff_t received = transport.read(buffer.data(), buffer.size());
    if (received < 0) {
      std::fprintf(stderr, "oak-barrels: reading the session failed\n");
      return 1;
    }
    if (received == 0) break;  // the host closed its end

    decoder.feed(buffer.data(), static_cast<std::size_t>(received));

    while (auto frame = decoder.next()) {
      if (!session.handle(*frame)) {
        running = false;
        break;
      }
    }

    if (decoder.failed()) {
      // The stream can no longer be trusted to be frame-aligned, so there is
      // nothing safe to say on it. Close, and explain on stderr.
      std::fprintf(stderr, "oak-barrels: protocol violation: %s\n",
                   oak::relay::protocol::decodeErrorSummary(decoder.error()));
      return 1;
    }
  }

  return 0;
}
