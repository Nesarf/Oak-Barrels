#include "harness.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "relay/protocol/frame.hpp"

namespace {

using oak::relay::protocol::DecodeError;
using oak::relay::protocol::encodeFrame;
using oak::relay::protocol::Frame;
using oak::relay::protocol::FrameDecoder;
using oak::relay::protocol::isKnownMessageType;
using oak::relay::protocol::kMaxControlFrameBytes;
using oak::relay::protocol::MessageType;

/// A four-byte little-endian length prefix followed by `type`.
std::vector<std::uint8_t> header(std::uint32_t length, std::uint8_t type) {
  return {static_cast<std::uint8_t>(length & 0xffu),
          static_cast<std::uint8_t>((length >> 8) & 0xffu),
          static_cast<std::uint8_t>((length >> 16) & 0xffu),
          static_cast<std::uint8_t>((length >> 24) & 0xffu),
          type};
}

}  // namespace

// ------------------------------------------------------------------- encoding

OAK_TEST(frame_encodes_the_length_prefix_little_endian) {
  const auto bytes = encodeFrame(MessageType::CapsRequest, "{}");
  OAK_CHECK_EQ(bytes.size(), 7u);  // 4 header + 1 type + 2 payload
  // `len` counts the type byte plus the payload: 1 + 2 = 3.
  OAK_CHECK_EQ(bytes[0], 0x03);
  OAK_CHECK_EQ(bytes[1], 0x00);
  OAK_CHECK_EQ(bytes[2], 0x00);
  OAK_CHECK_EQ(bytes[3], 0x00);
  OAK_CHECK_EQ(bytes[4], 0x10);
  OAK_CHECK_EQ(bytes[5], '{');
  OAK_CHECK_EQ(bytes[6], '}');
}

OAK_TEST(frame_encodes_a_payloadless_message_with_length_one) {
  const auto bytes = encodeFrame(MessageType::Bye);
  OAK_CHECK_EQ(bytes.size(), 5u);
  OAK_CHECK_EQ(bytes[0], 0x01);
  OAK_CHECK_EQ(bytes[4], 0x70);
}

OAK_TEST(frame_encodes_a_multi_byte_length) {
  const std::string payload(300, 'x');
  const auto bytes = encodeFrame(MessageType::Status, payload);
  // 1 + 300 = 301 = 0x012d
  OAK_CHECK_EQ(bytes[0], 0x2d);
  OAK_CHECK_EQ(bytes[1], 0x01);
  OAK_CHECK_EQ(bytes[2], 0x00);
  OAK_CHECK_EQ(bytes[3], 0x00);
}

// ------------------------------------------------------------------- decoding

OAK_TEST(frame_round_trips) {
  const auto bytes = encodeFrame(MessageType::Post, R"({"handle":42})");

  FrameDecoder decoder;
  decoder.feed(bytes);

  const auto frame = decoder.next();
  OAK_CHECK(frame.has_value());
  OAK_CHECK(frame->type == MessageType::Post);
  OAK_CHECK_EQ(frame->payload, std::string(R"({"handle":42})"));

  OAK_CHECK(!decoder.next().has_value());
  OAK_CHECK(!decoder.failed());
  OAK_CHECK_EQ(decoder.bufferedBytes(), 0u);
}

OAK_TEST(frame_round_trips_a_payloadless_message) {
  FrameDecoder decoder;
  decoder.feed(encodeFrame(MessageType::Bye));

  const auto frame = decoder.next();
  OAK_CHECK(frame.has_value());
  OAK_CHECK(frame->type == MessageType::Bye);
  OAK_CHECK(frame->payload.empty());
}

OAK_TEST(frame_decodes_two_frames_from_one_feed) {
  std::vector<std::uint8_t> both = encodeFrame(MessageType::Hello, R"({"revisions":[1]})");
  const auto second = encodeFrame(MessageType::Bye);
  both.insert(both.end(), second.begin(), second.end());

  FrameDecoder decoder;
  decoder.feed(both);

  const auto first = decoder.next();
  OAK_CHECK(first.has_value());
  OAK_CHECK(first->type == MessageType::Hello);

  const auto next = decoder.next();
  OAK_CHECK(next.has_value());
  OAK_CHECK(next->type == MessageType::Bye);

  OAK_CHECK(!decoder.next().has_value());
  OAK_CHECK_EQ(decoder.bufferedBytes(), 0u);
}

OAK_TEST(frame_decodes_a_frame_split_across_feeds) {
  const auto bytes = encodeFrame(MessageType::Open, R"({"kind":"emitter"})");

  FrameDecoder decoder;

  // A partial header must not be mistaken for a frame.
  decoder.feed(bytes.data(), 3);
  OAK_CHECK(!decoder.next().has_value());
  OAK_CHECK(!decoder.failed());
  OAK_CHECK_EQ(decoder.bufferedBytes(), 3u);

  // One byte at a time from here, so the decoder is exercised on every
  // possible split point rather than just one.
  for (std::size_t i = 3; i < bytes.size(); ++i) {
    decoder.feed(bytes.data() + i, 1);
    if (i + 1 < bytes.size()) {
      OAK_CHECK(!decoder.next().has_value());
      OAK_CHECK(!decoder.failed());
    }
  }

  const auto frame = decoder.next();
  OAK_CHECK(frame.has_value());
  OAK_CHECK(frame->type == MessageType::Open);
  OAK_CHECK_EQ(frame->payload, std::string(R"({"kind":"emitter"})"));
  OAK_CHECK_EQ(decoder.bufferedBytes(), 0u);
}

// ---------------------------------------------------------------- violations

OAK_TEST(frame_rejects_a_length_below_one) {
  FrameDecoder decoder;
  decoder.feed(header(0, 0x70));

  OAK_CHECK(!decoder.next().has_value());
  OAK_CHECK(decoder.failed());
  OAK_CHECK(decoder.error() == DecodeError::LengthTooSmall);
}

OAK_TEST(frame_rejects_a_frame_over_the_control_budget) {
  FrameDecoder decoder;
  decoder.feed(header(kMaxControlFrameBytes + 1, 0x70));

  OAK_CHECK(!decoder.next().has_value());
  OAK_CHECK(decoder.error() == DecodeError::FrameTooLarge);
}

OAK_TEST(frame_accepts_a_length_exactly_at_the_budget) {
  FrameDecoder decoder;
  decoder.feed(header(kMaxControlFrameBytes, 0x70));

  // Nothing is wrong yet -- the frame simply has not arrived.
  OAK_CHECK(!decoder.next().has_value());
  OAK_CHECK(!decoder.failed());
}

OAK_TEST(frame_rejects_an_unknown_message_type) {
  FrameDecoder decoder;
  decoder.feed(header(1, 0x03));

  OAK_CHECK(!decoder.next().has_value());
  OAK_CHECK(decoder.error() == DecodeError::UnknownMessageType);
}

OAK_TEST(frame_failure_is_sticky_and_ignores_later_input) {
  FrameDecoder decoder;
  decoder.feed(header(0, 0x70));
  OAK_CHECK(!decoder.next().has_value());
  OAK_CHECK(decoder.failed());

  // A well-formed frame arriving after a violation must not revive the stream:
  // the byte boundaries can no longer be trusted.
  decoder.feed(encodeFrame(MessageType::Bye));
  OAK_CHECK(!decoder.next().has_value());
  OAK_CHECK(decoder.failed());
  OAK_CHECK(decoder.error() == DecodeError::LengthTooSmall);
}

OAK_TEST(frame_ignores_an_empty_feed) {
  FrameDecoder decoder;
  decoder.feed(nullptr, 0);
  OAK_CHECK(!decoder.failed());
  OAK_CHECK_EQ(decoder.bufferedBytes(), 0u);
}

// ------------------------------------------------------------------ type table

OAK_TEST(frame_known_message_types_match_the_specification) {
  // These values are normative (protocol section 3.1) and must not drift.
  OAK_CHECK(isKnownMessageType(0x01));
  OAK_CHECK(isKnownMessageType(0x02));
  OAK_CHECK(isKnownMessageType(0x10));
  OAK_CHECK(isKnownMessageType(0x11));
  OAK_CHECK(isKnownMessageType(0x20));
  OAK_CHECK(isKnownMessageType(0x21));
  OAK_CHECK(isKnownMessageType(0x30));
  OAK_CHECK(isKnownMessageType(0x31));
  OAK_CHECK(isKnownMessageType(0x40));
  OAK_CHECK(isKnownMessageType(0x50));
  OAK_CHECK(isKnownMessageType(0x60));
  OAK_CHECK(isKnownMessageType(0x61));
  OAK_CHECK(isKnownMessageType(0x70));
  OAK_CHECK(isKnownMessageType(0x7f));

  OAK_CHECK(!isKnownMessageType(0x00));
  OAK_CHECK(!isKnownMessageType(0x03));
  OAK_CHECK(!isKnownMessageType(0x7e));
  OAK_CHECK(!isKnownMessageType(0xff));
}

OAK_TEST(frame_decode_errors_have_descriptions) {
  OAK_CHECK(std::string(oak::relay::protocol::decodeErrorSummary(DecodeError::None)).size() > 0);
  OAK_CHECK(std::string(oak::relay::protocol::decodeErrorSummary(DecodeError::FrameTooLarge)).size() > 0);
  OAK_CHECK(std::string(oak::relay::protocol::decodeErrorSummary(DecodeError::LengthTooSmall)).size() > 0);
  OAK_CHECK(std::string(oak::relay::protocol::decodeErrorSummary(DecodeError::UnknownMessageType)).size() > 0);
}
