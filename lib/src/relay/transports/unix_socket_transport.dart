/// Unix domain socket transport: the host connects to a station already
/// listening.
///
/// The station does not choose the path. The host does, and passes it with
/// `--listen`, because protocol section 2 is explicit that a name the relay
/// invents is a name anything else on the machine can guess and connect to.
///
/// Not available on Windows, where the platform's equivalent is a named pipe.
/// Connecting to one needs a platform call Dart does not expose, so this class
/// refuses rather than pretending -- and a host there should spawn the station
/// and speak over its streams, which works everywhere.
library;

import 'dart:async';
import 'dart:io';

import '../transport.dart';

/// A [RelayTransport] backed by a unix domain socket.
class UnixSocketTransport implements RelayTransport {
  UnixSocketTransport._(this._socket);

  final Socket _socket;
  bool _closed = false;

  /// Connects to a station listening at [path].
  ///
  /// Throws [UnsupportedError] where unix domain sockets do not exist, and
  /// [SocketException] when nothing is listening at [path].
  static Future<UnixSocketTransport> connect(String path) async {
    if (Platform.isWindows) {
      throw UnsupportedError(
        'unix domain sockets are not available on this platform',
      );
    }
    if (path.isEmpty) {
      throw ArgumentError.value(path, 'path', 'a socket path is required');
    }

    final socket = await Socket.connect(
      InternetAddress(path, type: InternetAddressType.unix),
      0,
    );
    return UnixSocketTransport._(socket);
  }

  @override
  Stream<List<int>> get incoming => _socket;

  @override
  void send(List<int> bytes) {
    if (_closed) return;
    _socket.add(bytes);
  }

  @override
  Future<void> close() async {
    if (_closed) return;
    _closed = true;
    try {
      await _socket.close();
    } on Object {
      // The station may already have gone away, which is a normal end state.
    }
  }
}
