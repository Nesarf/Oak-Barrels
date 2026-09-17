import 'dart:convert';
import 'dart:typed_data';

import 'package:oak_barrels/oak_barrels.dart';
import 'package:test/test.dart';

void main() {
  group('Frame.encode', () {
    test('length field counts the type byte, not just the payload', () {
      final bytes = const Frame(MessageType.bye).encode();
      expect(bytes.length, 5);

      final length = ByteData.sublistView(bytes, 0, 4).getUint32(0, Endian.little);
      expect(length, 1, reason: 'a bodyless frame is exactly the type byte');
      expect(bytes[4], MessageType.bye.code);
    });

    test('length prefix is little-endian', () {
      // 0x0102 encodes as 02 01 00 00 when little-endian.
      final bytes = const Frame(MessageType.bye, <String, Object?>{'x': 'y'}).encode();
      expect(bytes[0], greaterThan(1));
      expect(bytes[1], 0);
      expect(bytes[2], 0);
      expect(bytes[3], 0);
    });

    test('round-trips a payload through the decoder', () {
      const payload = <String, Object?>{'revisions': <int>[1], 'features': <String>['a']};
      final decoder = FrameDecoder();
      final frames = decoder.add(const Frame(MessageType.hello, payload).encode());

      expect(frames, hasLength(1));
      expect(frames.single.type, MessageType.hello);
      expect(frames.single.payload['revisions'], <int>[1]);
    });

    test('refuses to build a frame beyond the control budget', () {
      final huge = <String, Object?>{'blob': 'x' * (maxControlFrameBytes + 1)};
      expect(
        () => Frame(MessageType.post, huge).encode(),
        throwsA(isA<FrameTooLarge>()),
      );
    });
  });

  group('FrameDecoder', () {
    test('reassembles a frame split across arbitrary chunk boundaries', () {
      final encoded = const Frame(MessageType.openAck, <String, Object?>{'handle': 42}).encode();
      final decoder = FrameDecoder();

      // Feed one byte at a time — the worst case a transport can produce.
      final collected = <Frame>[];
      for (final byte in encoded) {
        collected.addAll(decoder.add(<int>[byte]));
      }

      expect(collected, hasLength(1));
      expect(collected.single.payload['handle'], 42);
      expect(decoder.bufferedBytes, 0);
    });

    test('decodes two frames delivered in a single chunk', () {
      final both = <int>[
        ...const Frame(MessageType.capsRequest).encode(),
        ...const Frame(MessageType.bye).encode(),
      ];
      final frames = FrameDecoder().add(both);

      expect(frames.map((f) => f.type), <MessageType>[
        MessageType.capsRequest,
        MessageType.bye,
      ]);
    });

    test('retains an incomplete tail across calls', () {
      final encoded = const Frame(MessageType.status, <String, Object?>{'reason': 0}).encode();
      final decoder = FrameDecoder();

      final first = decoder.add(encoded.sublist(0, 3));
      expect(first, isEmpty);
      expect(decoder.bufferedBytes, 3);

      final rest = decoder.add(encoded.sublist(3));
      expect(rest, hasLength(1));
      expect(decoder.bufferedBytes, 0);
    });

    test('rejects a zero-length frame', () {
      final zero = Uint8List.fromList(<int>[0, 0, 0, 0, 0x70]);
      expect(() => FrameDecoder().add(zero), throwsA(isA<ProtocolViolation>()));
    });

    test('rejects a frame larger than the control budget', () {
      final oversized = Uint8List(8);
      ByteData.view(oversized.buffer).setUint32(0, maxControlFrameBytes + 1, Endian.little);
      expect(() => FrameDecoder().add(oversized), throwsA(isA<ProtocolViolation>()));
    });

    test('rejects an unknown message type', () {
      final unknown = Uint8List.fromList(<int>[1, 0, 0, 0, 0x99]);
      expect(() => FrameDecoder().add(unknown), throwsA(isA<ProtocolViolation>()));
    });

    test('rejects a payload that is not a JSON object', () {
      final body = utf8.encode('[1,2,3]');
      final frame = Uint8List(4 + 1 + body.length);
      ByteData.view(frame.buffer).setUint32(0, 1 + body.length, Endian.little);
      frame[4] = MessageType.status.code;
      frame.setRange(5, frame.length, body);

      expect(() => FrameDecoder().add(frame), throwsA(isA<ProtocolViolation>()));
    });
  });

  group('ReasonCode', () {
    test('maps every defined code back to itself', () {
      for (final reason in ReasonCode.values) {
        expect(ReasonCode.tryFromCode(reason.code), reason);
      }
    });

    test('returns null for an unknown code instead of throwing', () {
      // A host built against an older revision must degrade, not crash, when a
      // newer relay sends a code it has never heard of.
      expect(ReasonCode.tryFromCode(9999), isNull);
    });

    test('summaries carry no identifying text', () {
      // This is the "quiet by default" rule from the protocol made executable:
      // a reason code may say what class of problem occurred, never which
      // vendor, version or path was involved.
      final identifying = RegExp(
        r'([A-Za-z]:[\\/]|/home/|/Users/|/opt/|\b\d+\.\d+\.\d+\b)',
      );
      for (final reason in ReasonCode.values) {
        expect(
          identifying.hasMatch(reason.summary),
          isFalse,
          reason: 'reason "${reason.name}" has an identifying summary: '
              '"${reason.summary}"',
        );
      }
    });
  });
}
