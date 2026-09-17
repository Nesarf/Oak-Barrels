/// Transport abstraction for the relay protocol.
///
/// The protocol specifies three transports (protocol section 2): a Unix domain
/// socket, a Windows named pipe, and stdio. They differ only in how bytes move;
/// everything above this interface is identical.
///
/// A transport is deliberately dumb: it moves bytes and nothing else. It does
/// not frame, does not parse, does not retry, and does not know what a relay
/// is. All of that lives above it, so a new transport is a small class rather
/// than a new implementation of the protocol.
library;

import 'dart:async';

/// A full-duplex, byte-oriented channel to a relay station.
abstract interface class RelayTransport {
  /// Bytes arriving from the relay.
  ///
  /// A single stream event may contain a partial frame, several frames, or
  /// both. The decoder above handles that; transports must not attempt to
  /// align events to frame boundaries.
  Stream<List<int>> get incoming;

  /// Sends bytes to the relay.
  ///
  /// Implementations may buffer. There is no completion signal: the protocol is
  /// paired request/response, so callers wait for a reply rather than for a
  /// write to land.
  void send(List<int> bytes);

  /// Releases the channel.
  ///
  /// Idempotent. Does not throw when the far end has already gone away -- a
  /// relay that has exited is a normal end state, not an error.
  Future<void> close();
}
