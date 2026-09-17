/// Stdio transport: the host spawns the relay and speaks over its streams.
///
/// This is the transport to reach for first (protocol section 2). It needs no socket,
/// no filesystem artifact, no permissions and no cleanup path, so it works
/// identically on every platform the relay supports -- which makes it the right
/// thing to validate the protocol against.
///
/// The relay writes nothing but protocol frames to stdout. Anything it wants to
/// say to a human goes to stderr, which this transport deliberately does not
/// consume: it is inherited so diagnostics reach the terminal the host was
/// launched from.
library;

import 'dart:async';
import 'dart:io';

import '../transport.dart';

/// A [RelayTransport] backed by a child process's standard streams.
class StdioTransport implements RelayTransport {
  StdioTransport._(this._process);

  final Process _process;
  bool _closed = false;

  /// Spawns [executable] and returns a transport to it.
  ///
  /// [workingDirectory] is passed through so a host can point the relay at a
  /// location of its choosing; the relay itself is stateless and writes nothing
  /// unless told to.
  static Future<StdioTransport> spawn(
    String executable, {
    List<String> arguments = const <String>[],
    String? workingDirectory,
    Map<String, String>? environment,
  }) async {
    final process = await Process.start(
      executable,
      arguments,
      workingDirectory: workingDirectory,
      environment: environment,
      // Protocol bytes on stdout; anything human-facing on stderr so it lands
      // in the host's terminal rather than corrupting the frame stream.
      mode: ProcessStartMode.normal,
    );
    return StdioTransport._(process);
  }

  /// The underlying process, for a host that needs its pid or exit code.
  Process get process => _process;

  @override
  Stream<List<int>> get incoming => _process.stdout;

  @override
  void send(List<int> bytes) {
    if (_closed) return;
    _process.stdin.add(bytes);
  }

  @override
  Future<void> close() async {
    if (_closed) return;
    _closed = true;
    try {
      await _process.stdin.close();
    } on Object {
      // The relay may already have exited. That is a normal end state.
    }
  }
}
