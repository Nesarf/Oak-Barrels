/// The host-side relay client.
///
/// The protocol is paired: `HELLO`/`HELLO_ACK`, `CAPS_REQUEST`/`CAPS_REPLY`,
/// `OPEN`/`OPEN_ACK`, and so on (protocol §3.1). This client therefore keeps
/// one outstanding request at a time. That is a deliberate simplification —
/// pipelining would need request identifiers, which revision 1 does not define.
///
/// Note what is *not* here: no version, no path, no vendor name, and no
/// assumption about what is on the other end. The client asks for capabilities
/// as classes and lets the station decide what it can do.
library;

import 'dart:async';

import 'frame.dart';
import 'reason.dart';
import 'transport.dart';
import 'transports/stdio_transport.dart';

/// Default ceiling on how long a single request may take.
///
/// Generous, because discovery and dynamic binding on a cold machine can take
/// real time. A timeout is a diagnostic event, not a routine one.
const Duration defaultRequestTimeout = Duration(seconds: 30);

/// The outcome of a capability query.
class Capabilities {

  factory Capabilities.fromPayload(Map<String, Object?> payload) {
    final rawClasses = payload['classes'];
    final rawLimits = payload['limits'];

    final limits = <String, num>{};
    if (rawLimits is Map) {
      for (final entry in rawLimits.entries) {
        final value = entry.value;
        if (value is num) limits['${entry.key}'] = value;
      }
    }

    return Capabilities(
      classes: rawClasses is List
          ? rawClasses.whereType<String>().toSet()
          : const <String>{},
      limits: limits,
      bulk: payload['bulk'] == true,
    );
  }
  const Capabilities({
    required this.classes,
    required this.limits,
    required this.bulk,
  });

  /// Compatibility classes offered by the station, e.g. `engine.action.named`.
  ///
  /// These are *classes*, never versions. A host that does not recognise a
  /// class treats it as absent rather than fatal.
  final Set<String> classes;

  /// Numeric limits, e.g. `max_targets`, `max_param_rate_hz`.
  final Map<String, num> limits;

  /// Whether a bulk audio channel could be negotiated on a separate transport.
  final bool bulk;

  /// Whether the station advertises [capabilityClass].
  bool supports(String capabilityClass) => classes.contains(capabilityClass);

  @override
  String toString() =>
      'Capabilities(classes: ${classes.length}, limits: $limits, bulk: $bulk)';
}

/// A live session with a relay station.
class Relay {
  Relay._(this._transport) {
    _subscription = _transport.incoming.listen(
      _onBytes,
      onError: _fail,
      onDone: () => _fail(const RelayClosed('the relay closed the channel')),
      cancelOnError: true,
    );
  }

  /// Wraps an already-connected [transport].
  static Future<Relay> attach(RelayTransport transport) async => Relay._(transport);

  /// Spawns a relay process and wraps its standard streams.
  static Future<Relay> spawn(
    String executable, {
    List<String> arguments = const <String>[],
    String? workingDirectory,
    Map<String, String>? environment,
  }) async {
    final transport = await StdioTransport.spawn(
      executable,
      arguments: arguments,
      workingDirectory: workingDirectory,
      environment: environment,
    );
    return Relay._(transport);
  }

  final RelayTransport _transport;
  late final StreamSubscription<List<int>> _subscription;

  final List<Frame> _queued = <Frame>[];
  Completer<Frame>? _waiter;
  Object? _failure;

  /// The protocol revision agreed during [negotiate], or null before that.
  int? get revision => _revision;
  int? _revision;

  // ---------------------------------------------------------------- inbound

  void _onBytes(List<int> chunk) {
    final List<Frame> frames;
    try {
      frames = _decoder.add(chunk);
    } on ProtocolViolation catch (error) {
      _fail(error);
      return;
    }

    for (final frame in frames) {
      final waiter = _waiter;
      if (waiter != null && !waiter.isCompleted) {
        _waiter = null;
        waiter.complete(frame);
      } else {
        _queued.add(frame);
      }
    }
  }

  final FrameDecoder _decoder = FrameDecoder();

  void _fail(Object error) {
    if (_failure != null) return;
    _failure = error;
    final waiter = _waiter;
    if (waiter != null && !waiter.isCompleted) {
      _waiter = null;
      waiter.completeError(error);
    }
  }

  Future<Frame> _receive(Duration timeout) {
    if (_queued.isNotEmpty) return Future<Frame>.value(_queued.removeAt(0));
    if (_failure != null) return Future<Frame>.error(_failure!);

    final waiter = Completer<Frame>();
    _waiter = waiter;
    return waiter.future.timeout(
      timeout,
      onTimeout: () {
        if (identical(_waiter, waiter)) _waiter = null;
        throw RelayTimeout(timeout);
      },
    );
  }

  Future<Frame> _request(Frame request, Duration timeout) async {
    if (_failure != null) throw _failure!;
    if (_failure == null && _subscription.isPaused) {
      throw const RelayClosed('the relay channel is not readable');
    }

    _transport.send(request.encode());
    final response = await _receive(timeout);

    if (response.type == MessageType.error) {
      throw RelayException.fromPayload(response.payload);
    }
    return response;
  }

  // --------------------------------------------------------------- protocol

  /// Performs `HELLO`/`HELLO_ACK` and records the agreed revision.
  ///
  /// Returns the selected revision. Throws [RelayException] with
  /// [ReasonCode.noCommonRevision] when our revision set and the station's do
  /// not overlap — the station decides, we do not guess.
  Future<int> negotiate({
    Set<int> revisions = supportedRevisions,
    Duration timeout = defaultRequestTimeout,
  }) async {
    final response = await _request(
      Frame(MessageType.hello, <String, Object?>{
        'revisions': revisions.toList()..sort(),
      }),
      timeout,
    );
    if (response.type != MessageType.helloAck) {
      throw RelayException(
        null,
        'expected HELLO_ACK, got ${response.type.name}',
      );
    }
    final selected = response.payload['revision'];
    if (selected is! int) {
      throw const RelayException(null, 'HELLO_ACK carried no integer revision');
    }
    _revision = selected;
    return selected;
  }

  /// Asks what the station can do, as compatibility classes.
  Future<Capabilities> capabilities({
    Duration timeout = defaultRequestTimeout,
  }) async {
    final response = await _request(
      const Frame(MessageType.capsRequest),
      timeout,
    );
    if (response.type != MessageType.capsReply) {
      throw RelayException(null, 'expected CAPS_REPLY, got ${response.type.name}');
    }
    return Capabilities.fromPayload(response.payload);
  }

  /// Opens a target and returns its opaque handle.
  ///
  /// [kind] is generic (`emitter`, `bus`, `global`); the station maps it to
  /// whatever the backing engine calls that concept.
  Future<int> open(
    String kind, {
    String? name,
    Duration timeout = defaultRequestTimeout,
  }) async {
    final response = await _request(
      Frame(MessageType.open, <String, Object?>{
        'kind': kind,
        if (name != null) 'name': name,
      }),
      timeout,
    );
    if (response.type != MessageType.openAck) {
      throw RelayException(null, 'expected OPEN_ACK, got ${response.type.name}');
    }
    final handle = response.payload['handle'];
    if (handle is! int || handle == 0) {
      throw const RelayException(null, 'OPEN_ACK carried no usable handle');
    }
    return handle;
  }

  /// Fires a named action at [handle].
  ///
  /// The name is resolved by the backing engine; this client carries no
  /// catalogue of valid names, because such a catalogue would be a
  /// version-specific fact.
  Future<void> post(
    int handle,
    String action, {
    Map<String, Object?>? args,
    Duration timeout = defaultRequestTimeout,
  }) async {
    await _request(
      Frame(MessageType.post, <String, Object?>{
        'handle': handle,
        'action': action,
        if (args != null) 'args': args,
      }),
      timeout,
    );
  }

  /// Sets a continuous parameter — the hook for real-time control.
  Future<void> set(
    int handle,
    String parameter,
    double value, {
    Duration timeout = defaultRequestTimeout,
  }) async {
    await _request(
      Frame(MessageType.set, <String, Object?>{
        'handle': handle,
        'param': parameter,
        'value': value,
      }),
      timeout,
    );
  }

  /// Closes a target. Idempotent per protocol §5: closing an unknown handle is
  /// not an error.
  Future<void> close(
    int handle, {
    Duration timeout = defaultRequestTimeout,
  }) async {
    await _request(
      Frame(MessageType.close, <String, Object?>{'handle': handle}),
      timeout,
    );
  }

  /// Sends `BYE` and releases the transport.
  Future<void> shutdown({
    Duration timeout = defaultRequestTimeout,
  }) async {
    try {
      await _request(const Frame(MessageType.bye), timeout);
    } on Object {
      // The station may simply have gone away. Shutting down must not throw.
    }
    await dispose();
  }

  /// Drops the connection without a farewell.
  Future<void> dispose() async {
    await _subscription.cancel();
    await _transport.close();
  }
}

/// A failure reported by the station, carrying a protocol reason code.
class RelayException implements Exception {

  factory RelayException.fromPayload(Map<String, Object?> payload) {
    final code = payload['reason'];
    final detail = payload['detail'];
    return RelayException(
      code is int ? ReasonCode.tryFromCode(code) : null,
      detail is String ? detail : null,
    );
  }
  const RelayException(this.reason, this.detail);

  /// The reason code, or null when the station sent something malformed.
  final ReasonCode? reason;

  /// A short, non-identifying description. Per protocol §6 this must not name a
  /// vendor, a version or a path.
  final String? detail;

  @override
  String toString() {
    final code = reason?.name ?? 'unknown';
    return detail == null ? 'RelayException($code)' : 'RelayException($code): $detail';
  }
}

/// Raised when a request received no reply within its budget.
class RelayTimeout implements Exception {
  const RelayTimeout(this.timeout);

  final Duration timeout;

  @override
  String toString() => 'RelayTimeout: no reply within ${timeout.inMilliseconds} ms';
}

/// Raised when the channel closed unexpectedly.
class RelayClosed implements Exception {
  const RelayClosed(this.message);

  final String message;

  @override
  String toString() => 'RelayClosed: $message';
}
