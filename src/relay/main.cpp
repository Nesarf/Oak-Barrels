// The relay station.
//
// Serves exactly one session, over one of three transports (protocol section 2):
//
//   stdio         the host spawns this binary and speaks over its streams
//   unix socket   the host names a path; this process binds it and waits
//   named pipe    the host names a pipe; this process creates it and waits
//
// The transport is chosen by the host. The station invents no socket path and
// no pipe name, because a name the relay picks is a name anything else on the
// machine can guess.
//
// A second endpoint may be nominated for audio (protocol section 7). It is a
// separate transport on purpose: a host that stops reading its audio must stall
// its audio and nothing else.
//
// Exit codes:
//   0  clean end of session
//   1  the peer violated the protocol, or a transport failed
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
#include "relay/probe/probing_backend.hpp"
#include "relay/protocol/frame.hpp"
#include "relay/session.hpp"
#include "relay/transport/named_pipe_transport.hpp"
#include "relay/transport/stdio_transport.hpp"
#include "relay/transport/transport.hpp"
#include "relay/transport/unix_socket_transport.hpp"

#ifndef OAK_BARRELS_VERSION
#define OAK_BARRELS_VERSION "unversioned"
#endif

namespace {

const char* kUsage =
    "oak-barrels - a neutral relay between a host and an installed audio engine.\n"
    "\n"
    "Usage:\n"
    "  oak-barrels [transport] [--bulk <name>] [--search-root <dir>]...\n"
    "              [--probe-profile <f>] [--version] [--help]\n"
    "\n"
    "Control transport, choose at most one. The default is standard input and\n"
    "output, which is what a host that spawns this process wants.\n"
    "\n"
    "  --listen <path>      serve one connection on a unix domain socket.\n"
    "  --pipe <name>        serve one client on a named pipe.\n"
    "\n"
    "  --bulk <name>        also serve the audio channel of protocol section 7\n"
    "                       on a second endpoint, of whichever kind this\n"
    "                       platform provides. Only used if a bound engine's\n"
    "                       profile declares a sink to install.\n"
    "  --search-root <dir>  nominate a directory for discovery. Repeatable.\n"
    "                       The station invents no locations of its own and\n"
    "                       looks nowhere unless asked, so with no\n"
    "                       --search-root it never touches the filesystem.\n"
    "  --probe-profile <f>  a JSON file describing how to talk to an engine.\n"
    "                       Needs at least one --search-root. The symbol\n"
    "                       names come from that file and never from this\n"
    "                       build, which is how the station can bind an\n"
    "                       engine it has never heard of.\n"
    "\n"
    "The control channel carries frames and nothing else. Diagnostics go to\n"
    "standard error, which is never part of the protocol.\n";

/// Writes session frames to whichever transport is in use.
class TransportSink final : public oak::relay::FrameSink {
 public:
  explicit TransportSink(oak::relay::transport::Transport& transport)
      : transport_(transport) {}

  bool writeFrame(const std::vector<std::uint8_t>& bytes) override {
    return transport_.write(bytes);
  }

 private:
  oak::relay::transport::Transport& transport_;
};

/// Reports a transport setup failure, which is always the host's to fix.
int failSetup(const std::string& error) {
  std::fprintf(stderr, "oak-barrels: %s\n", error.c_str());
  return 2;
}

/// Binds [name] and waits for the one peer that will use it.
///
/// The kind is the platform's own: a unix domain socket where those exist, a
/// named pipe where they do not. Which one it is does not reach the protocol,
/// so a host gets the natural thing without the relay having to ask.
std::unique_ptr<oak::relay::transport::Transport> openEndpoint(const std::string& name,
                                                               std::string& error) {
#ifdef _WIN32
  auto endpoint = oak::relay::transport::NamedPipeTransport::prepare(name, error);
#else
  auto endpoint = oak::relay::transport::UnixSocketTransport::prepare(name, error);
#endif
  if (!endpoint) return nullptr;

  // Waiting is reported, because a host that forgot to connect would otherwise
  // see the process simply stop.
  std::fprintf(stderr, "oak-barrels: waiting for one connection\n");
  if (!endpoint->acceptOne(error)) return nullptr;

  return endpoint;
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> searchRoots;
  std::string profilePath;
  std::string listenPath;
  std::string pipeName;
  std::string bulkName;

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
    if (argument == "--search-root" || argument == "--probe-profile" ||
        argument == "--listen" || argument == "--pipe" || argument == "--bulk") {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "oak-barrels: %s needs a value\n", argument.c_str());
        return 2;
      }
      const std::string value = argv[++i];

      if (argument == "--search-root") {
        searchRoots.push_back(value);
      } else if (argument == "--probe-profile") {
        profilePath = value;
      } else if (argument == "--listen") {
        listenPath = value;
      } else if (argument == "--pipe") {
        pipeName = value;
      } else {
        bulkName = value;
      }
      continue;
    }

    std::fprintf(stderr, "oak-barrels: unrecognised argument\n");
    return 2;
  }

  if ((!listenPath.empty() ? 1 : 0) + (!pipeName.empty() ? 1 : 0) > 1) {
    std::fprintf(stderr, "oak-barrels: choose one control transport, not two\n");
    return 2;
  }

  std::unique_ptr<oak::relay::transport::Transport> transport;
  if (!listenPath.empty()) {
    std::string error;
    transport = openEndpoint(listenPath, error);
    if (!transport) return failSetup(error);
  } else if (!pipeName.empty()) {
    std::string error;
    transport = openEndpoint(pipeName, error);
    if (!transport) return failSetup(error);
  } else {
    transport = std::make_unique<oak::relay::transport::StdioTransport>();
  }

  // The audio endpoint is opened and accepted before the session starts, for
  // the same reason the control one is: accepting on a worker would mean a
  // worker that cannot be interrupted, and a station that cannot be shut down
  // while a host declines to connect.
  std::unique_ptr<oak::relay::transport::Transport> audioEndpoint;
  if (!bulkName.empty()) {
    std::string error;
    audioEndpoint = openEndpoint(bulkName, error);
    if (!audioEndpoint) return failSetup(error);
  }

  TransportSink sink(*transport);

  std::vector<oak::relay::probe::Profile> profiles;
  if (!profilePath.empty()) {
    const auto loaded = oak::relay::probe::loadProfiles(profilePath);
    if (!loaded.has_value()) {
      // Refused outright rather than applied in part. Half a description of how
      // to call somebody's code is worse than none, because the half that is
      // missing is the half that would have crashed.
      std::fprintf(stderr, "oak-barrels: the probe profile could not be read\n");
      return 2;
    }
    profiles = *loaded;
  }

  // Discovery is host-directed and opt-in. With no nominated root, no
  // filesystem code runs at all -- which is the default posture, because a
  // relay that surveyed the machine unprompted would be doing the exact thing
  // this project exists to avoid.
  std::unique_ptr<oak::relay::EngineBackend> backend;
  if (searchRoots.empty()) {
    backend = std::make_unique<oak::relay::NullBackend>();
  } else if (profiles.empty()) {
    backend = std::make_unique<oak::relay::discovery::DiscoveryBackend>(
        std::move(searchRoots));
  } else {
    backend = std::make_unique<oak::relay::probe::ProbingBackend>(
        std::move(searchRoots), std::move(profiles), std::move(audioEndpoint));
  }

  oak::relay::Session session(*backend, sink);

  oak::relay::protocol::FrameDecoder decoder;
  std::vector<std::uint8_t> buffer(oak::relay::transport::kMaxIoChunk);

  bool running = true;
  while (running) {
    const std::ptrdiff_t received = transport->read(buffer.data(), buffer.size());
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
