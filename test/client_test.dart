import 'dart:async';

import 'package:oak_barrels/oak_barrels.dart';
import 'package:test/test.dart';

/// An in-memory transport that stands in for a relay process.
///
/// Using one of these instead of a real process keeps these tests about the
/// *client*, and lets them assert on malformed and hostile replies that a
/// well-behaved station would never send.
class FakeTransport implements RelayTransport {
  final StreamController<List<int>> _incoming = StreamController<List<int>>();
  final List<int> written = <int>[];
  bool closeCalled = false;

  @override
  Stream<List<int>> get incoming => _incoming.stream;

  @override
  void send(List<int> bytes) => written.addAll(bytes);

  @override
  Future<void> close() async {
    closeCalled = true;
    if (!_incoming.isClosed) await _incoming.close();
  }

  /// Simulates the station sending [frame].
  void reply(Frame frame) => _incoming.add(frame.encode());

  /// Simulates the station sending raw bytes, for malformed-stream tests.
  void replyRaw(List<int> bytes) => _incoming.add(bytes);

  /// Simulates the station exiting.
  void drop() {
    if (!_incoming.isClosed) _incoming.close();
  }

  /// Frames the client has sent so far.
  List<Frame> get sentFrames => FrameDecoder().add(written);
}

Future<Relay> connected(FakeTransport transport) => Relay.attach(transport);

void main() {
  group('negotiate', () {
    test('sends HELLO with our revisions and records the agreed one', () async {
      final transport = FakeTransport();
      final relay = await connected(transport);

      final future = relay.negotiate();
      await Future<void>.delayed(Duration.zero);

      final hello = transport.sentFrames.single;
      expect(hello.type, MessageType.hello);
      expect(hello.payload['revisions'], contains(1));

      transport.reply(
        const Frame(
          MessageType.helloAck,
          <String, Object?>{'revision': 1},
        ),
      );

      expect(await future, 1);
      expect(relay.revision, 1);
      await relay.dispose();
    });

    test('surfaces NO_COMMON_REVISION rather than guessing', () async {
      final transport = FakeTransport();
      final relay = await connected(transport);

      final future = relay.negotiate();
      await Future<void>.delayed(Duration.zero);
      transport.reply(
        const Frame(MessageType.error, <String, Object?>{
          'reason': 2,
          'detail': 'protocol revisions do not overlap',
        }),
      );

      await expectLater(
        future,
        throwsA(
          isA<RelayException>()
              .having((e) => e.reason, 'reason', ReasonCode.noCommonRevision),
        ),
      );
      await relay.dispose();
    });

    test('rejects a HELLO_ACK without an integer revision', () async {
      final transport = FakeTransport();
      final relay = await connected(transport);

      final future = relay.negotiate();
      await Future<void>.delayed(Duration.zero);
      transport.reply(const Frame(MessageType.helloAck, <String, Object?>{}));

      await expectLater(future, throwsA(isA<RelayException>()));
      await relay.dispose();
    });
  });

  group('capabilities', () {
    test('parses classes, limits and the bulk flag', () async {
      final transport = FakeTransport();
      final relay = await connected(transport);

      final future = relay.capabilities();
      await Future<void>.delayed(Duration.zero);
      transport.reply(
        const Frame(MessageType.capsReply, <String, Object?>{
          'classes': <String>['engine.action.named', 'engine.param.continuous'],
          'limits': <String, Object?>{'max_targets': 64, 'noise': 'ignored'},
          'bulk': false,
        }),
      );

      final caps = await future;
      expect(caps.supports('engine.action.named'), isTrue);
      expect(caps.supports('engine.bulk.pcm'), isFalse);
      expect(caps.limits['max_targets'], 64);
      expect(
        caps.limits.containsKey('noise'),
        isFalse,
        reason: 'non-numeric limits must be dropped, not coerced',
      );
      expect(caps.bulk, isFalse);
      await relay.dispose();
    });

    test('treats a missing classes field as empty rather than fatal', () async {
      final transport = FakeTransport();
      final relay = await connected(transport);

      final future = relay.capabilities();
      await Future<void>.delayed(Duration.zero);
      transport.reply(const Frame(MessageType.capsReply));

      final caps = await future;
      expect(caps.classes, isEmpty);
      await relay.dispose();
    });
  });

  group('operations', () {
    test('open returns the handle from OPEN_ACK', () async {
      final transport = FakeTransport();
      final relay = await connected(transport);

      final future = relay.open('emitter', name: 'ui');
      await Future<void>.delayed(Duration.zero);
      expect(transport.sentFrames.single.payload['kind'], 'emitter');
      expect(transport.sentFrames.single.payload['name'], 'ui');

      transport.reply(
          const Frame(MessageType.openAck, <String, Object?>{'handle': 7}));
      expect(await future, 7);
      await relay.dispose();
    });

    test('rejects a zero handle, which the protocol reserves', () async {
      final transport = FakeTransport();
      final relay = await connected(transport);

      final future = relay.open('emitter');
      await Future<void>.delayed(Duration.zero);
      transport.reply(
          const Frame(MessageType.openAck, <String, Object?>{'handle': 0}));

      await expectLater(future, throwsA(isA<RelayException>()));
      await relay.dispose();
    });

    test('post carries action and args', () async {
      final transport = FakeTransport();
      final relay = await connected(transport);

      final future =
          relay.post(7, 'ice_drop', args: <String, Object?>{'gain': 0.5});
      await Future<void>.delayed(Duration.zero);
      final sent = transport.sentFrames.single;
      expect(sent.type, MessageType.post);
      expect(sent.payload['handle'], 7);
      expect(sent.payload['action'], 'ice_drop');

      transport.reply(
          const Frame(MessageType.status, <String, Object?>{'reason': 0}));
      await future;
      await relay.dispose();
    });

    test('set carries a continuous value', () async {
      final transport = FakeTransport();
      final relay = await connected(transport);

      final future = relay.set(7, 'intensity', 0.82);
      await Future<void>.delayed(Duration.zero);
      final sent = transport.sentFrames.single;
      expect(sent.type, MessageType.set);
      expect(sent.payload['param'], 'intensity');
      expect(sent.payload['value'], 0.82);

      transport.reply(
          const Frame(MessageType.status, <String, Object?>{'reason': 0}));
      await future;
      await relay.dispose();
    });

    test('unknown reason codes map to null instead of throwing', () async {
      final transport = FakeTransport();
      final relay = await connected(transport);

      final future = relay.post(1, 'anything');
      await Future<void>.delayed(Duration.zero);
      transport.reply(
          const Frame(MessageType.error, <String, Object?>{'reason': 4242}));

      await expectLater(
        future,
        throwsA(
            isA<RelayException>().having((e) => e.reason, 'reason', isNull)),
      );
      await relay.dispose();
    });
  });

  group('failure handling', () {
    test('a request that gets no reply times out', () async {
      final transport = FakeTransport();
      final relay = await connected(transport);

      await expectLater(
        relay.negotiate(timeout: const Duration(milliseconds: 30)),
        throwsA(isA<RelayTimeout>()),
      );
      await relay.dispose();
    });

    test('a malformed stream surfaces as the protocol violation it is',
        () async {
      final transport = FakeTransport();
      final relay = await connected(transport);

      final future = relay.negotiate();
      await Future<void>.delayed(Duration.zero);
      transport.replyRaw(<int>[0, 0, 0, 0, 0x70]); // zero-length frame

      await expectLater(future, throwsA(isA<ProtocolViolation>()));
      await relay.dispose();
    });

    test('a dropped channel fails pending and subsequent requests', () async {
      final transport = FakeTransport();
      final relay = await connected(transport);

      final first = relay.negotiate();
      await Future<void>.delayed(Duration.zero);
      transport.drop();

      await expectLater(first, throwsA(isA<RelayClosed>()));
      await expectLater(relay.capabilities(), throwsA(isA<RelayClosed>()));
      await relay.dispose();
    });

    test('shutdown tolerates a station that has already gone', () async {
      final transport = FakeTransport();
      final relay = await connected(transport);
      transport.drop();

      await expectLater(relay.shutdown(), completes);
      expect(transport.closeCalled, isTrue);
    });
  });
}
