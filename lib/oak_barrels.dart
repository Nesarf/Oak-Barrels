/// Oak Barrels -- a neutral relay client.
///
/// The relay station sits between a host and whatever audio engine is installed
/// on the machine. It belongs to neither side: no vendor headers, no vendor
/// libraries, nothing linked at build time. Compatibility is expressed as
/// classes rather than versions, and discovered facts stay in memory.
///
/// See `docs/RELAY_PROTOCOL.md` for the normative protocol specification.
///
/// This library is transport and protocol only. It does not depend on any UI
/// framework -- hosts are often UI applications, but that is a usage pattern,
/// not a requirement.
library;

export 'src/relay/client.dart';
export 'src/relay/frame.dart';
export 'src/relay/reason.dart';
export 'src/relay/transport.dart';
export 'src/relay/transports/stdio_transport.dart';
export 'src/relay/transports/unix_socket_transport.dart';
