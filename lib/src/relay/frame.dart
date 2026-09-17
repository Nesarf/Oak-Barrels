/// Wire framing for the relay protocol.
///
/// This file implements the framing rules of `docs/RELAY_PROTOCOL.md` §3 and
/// the message types of §3.1. Where this code and that document disagree, the
/// document is normative and this code is wrong.
///
/// Design notes that are easy to get wrong later:
///
/// * The length field counts the **type byte plus payload**, not the payload
///   alone. A frame carrying only a type has length 1.
/// * Integers are little-endian.
/// * A frame is length-prefixed rather than newline-delimited so that payloads
///   may legitimately contain newlines — names are chosen by the host and are
///   not constrained.
library;

import 'dart:convert';
import 'dart:typed_data';

/// Protocol revisions this implementation can speak.
///
/// A host offers its set in `HELLO`; the relay answers with the highest mutual
/// one. Bumping this set is how a future revision stays backwards compatible.
const Set<int> supportedRevisions = {1};

/// Budget for a single control frame.
///
/// Bulk audio deliberately does not travel over this pipe (protocol §7), so a
/// frame this large is always a protocol error rather than a big payload.
const int maxControlFrameBytes = 1024 * 1024;

/// Message type byte. See protocol §3.1.
enum MessageType {
  hello(0x01),
  helloAck(0x02),
  capsRequest(0x10),
  capsReply(0x11),
  open(0x20),
  openAck(0x21),
  post(0x30),
  set(0x31),
  close(0x40),
  status(0x50),
  diagRequest(0x60),
  diagReply(0x61),
  bye(0x70),
  error(0x7f);

  const MessageType(this.code);

  /// The byte written on the wire.
  final int code;

  /// Returns the type for [code], or null when the byte is not a known type.
  ///
  /// Unknown types are a protocol violation for now. If a future revision adds
  /// types, negotiators must agree on a revision that includes them, so an
  /// unknown byte at a negotiated revision genuinely is an error.
  static MessageType? tryFromCode(int code) {
    for (final type in MessageType.values) {
      if (type.code == code) return type;
    }
    return null;
  }
}

/// A single protocol frame: a type and an optional JSON payload.
class Frame {
  const Frame(this.type, [this.payload = const <String, Object?>{}]);

  final MessageType type;

  /// Payload as a JSON-compatible map. Empty for types that carry no body.
  final Map<String, Object?> payload;

  /// Encodes this frame, including its 4-byte length prefix.
  ///
  /// Throws [FrameTooLarge] when the encoded frame would exceed the control
  /// budget. This is checked before allocating, so a hostile or confused caller
  /// cannot make us reserve a gigabyte.
  Uint8List encode() {
    final body = payload.isEmpty
        ? Uint8List(0)
        : Uint8List.fromList(utf8.encode(jsonEncode(payload)));

    final length = 1 + body.length;
    if (length > maxControlFrameBytes) {
      throw FrameTooLarge(length, maxControlFrameBytes);
    }

    final out = Uint8List(4 + length);
    ByteData.view(out.buffer).setUint32(0, length, Endian.little);
    out[4] = type.code;
    if (body.isNotEmpty) {
      out.setRange(5, out.length, body);
    }
    return out;
  }

  @override
  String toString() => 'Frame(${type.name}, $payload)';
}

/// Incremental frame decoder.
///
/// Frames arrive in arbitrary chunks from any of the transports, so decoding
/// has to be resumable. Feed [add] whatever bytes arrived; it returns only the
/// frames that are now complete, retaining any partial tail for next time.
///
/// The buffer is a plain growable list rather than a `BytesBuilder` on purpose.
/// `BytesBuilder`'s take/get semantics are easy to misread — in particular it is
/// tempting to read the accumulated bytes and then append the leftover back,
/// which silently duplicates the buffer. Control messages are small and
/// infrequent, so clarity beats a micro-optimisation here.
class FrameDecoder {
  final List<int> _buffer = <int>[];

  /// Bytes held back because they do not yet form a complete frame.
  int get bufferedBytes => _buffer.length;

  /// Feeds [chunk] and returns every frame that became complete.
  ///
  /// Throws [ProtocolViolation] on a malformed stream. After such a throw the
  /// decoder state is meaningless; callers should close the session rather than
  /// attempt to resynchronise, since the framing has no delimiter to resync on.
  List<Frame> add(List<int> chunk) {
    _buffer.addAll(chunk);

    final frames = <Frame>[];
    var offset = 0;

    while (_buffer.length - offset >= 4) {
      final header = Uint8List.fromList(_buffer.sublist(offset, offset + 4));
      final length = ByteData.view(header.buffer).getUint32(0, Endian.little);

      if (length < 1) {
        throw ProtocolViolation('frame length $length is below the minimum of 1');
      }
      if (length > maxControlFrameBytes) {
        throw ProtocolViolation(
          'frame length $length exceeds the $maxControlFrameBytes byte control budget',
        );
      }
      if (_buffer.length - offset < 4 + length) {
        break; // Complete frame not yet available.
      }

      final typeByte = _buffer[offset + 4];
      final type = MessageType.tryFromCode(typeByte);
      if (type == null) {
        throw ProtocolViolation(
          'unknown message type 0x${typeByte.toRadixString(16).padLeft(2, '0')}',
        );
      }

      final bodyStart = offset + 5;
      final bodyEnd = offset + 4 + length;
      final payload = bodyStart == bodyEnd
          ? const <String, Object?>{}
          : _decodePayload(_buffer, bodyStart, bodyEnd);

      frames.add(Frame(type, payload));
      offset = bodyEnd;
    }

    if (offset > 0) {
      _buffer.removeRange(0, offset);
    }
    return frames;
  }

  static Map<String, Object?> _decodePayload(List<int> buffer, int start, int end) {
    final text = utf8.decode(buffer.sublist(start, end));
    final Object? decoded;
    try {
      decoded = jsonDecode(text);
    } on FormatException catch (error) {
      throw ProtocolViolation('payload is not valid UTF-8 JSON: ${error.message}');
    }
    if (decoded is! Map) {
      throw ProtocolViolation('payload JSON must be an object, got ${decoded.runtimeType}');
    }
    return decoded.cast<String, Object?>();
  }
}

/// Raised when a byte stream violates the framing rules.
class ProtocolViolation implements Exception {
  const ProtocolViolation(this.message);

  final String message;

  @override
  String toString() => 'ProtocolViolation: $message';
}

/// Raised when a frame would exceed the control-frame budget.
class FrameTooLarge implements Exception {
  const FrameTooLarge(this.size, this.limit);

  final int size;
  final int limit;

  @override
  String toString() => 'FrameTooLarge: $size bytes exceeds the $limit byte limit';
}
