// The audio path, tested without a platform underneath it.
//
// The queue is the only thing an engine's audio thread ever touches, so it is
// tested on its own and hard. The channel above it is tested against a
// transport that simply keeps what it is given, which is enough to prove the
// pump drains the queue and reports what it dropped.

#include "harness.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "relay/bulk/bulk_channel.hpp"
#include "relay/bulk/ring_buffer.hpp"
#include "relay/transport/transport.hpp"

namespace {

using oak::relay::bulk::BulkChannel;
using oak::relay::bulk::RingBuffer;
using oak::relay::transport::Transport;

std::vector<std::uint8_t> bytesOf(const std::string& text) {
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

std::string textOf(const std::vector<std::uint8_t>& bytes) {
  return std::string(bytes.begin(), bytes.end());
}

/// A transport that keeps what it is given.
class CollectingTransport final : public Transport {
 public:
  std::ptrdiff_t read(std::uint8_t* /*buffer*/, std::size_t /*maxBytes*/) override {
    return 0;
  }

  bool write(const std::vector<std::uint8_t>& bytes) override {
    const std::lock_guard<std::mutex> lock(mutex_);
    collected_.insert(collected_.end(), bytes.begin(), bytes.end());
    return true;
  }

  std::vector<std::uint8_t> collected() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return collected_;
  }

 private:
  mutable std::mutex mutex_;
  std::vector<std::uint8_t> collected_;
};

/// Waits until [predicate] holds, or gives up and says so.
template <typename Predicate>
bool waitFor(Predicate predicate, std::chrono::milliseconds limit) {
  const auto deadline = std::chrono::steady_clock::now() + limit;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return predicate();
}

}  // namespace

// ------------------------------------------------------------------ the queue

OAK_TEST(ring_buffer_round_trips) {
  RingBuffer ring(16);
  OAK_CHECK_EQ(ring.capacity(), 16u);
  OAK_CHECK(ring.empty());

  const auto data = bytesOf("hello");
  OAK_CHECK_EQ(ring.write(data.data(), data.size()), 5u);
  OAK_CHECK_EQ(ring.size(), 5u);

  std::vector<std::uint8_t> out(5, 0);
  OAK_CHECK_EQ(ring.read(out.data(), out.size()), 5u);
  OAK_CHECK(out == data);
  OAK_CHECK(ring.empty());
}

OAK_TEST(ring_buffer_holds_one_less_than_it_was_asked_for) {
  // One slot is deliberately kept free, because otherwise a full queue and an
  // empty one look identical.
  RingBuffer ring(4);
  const std::vector<std::uint8_t> data{'a', 'b', 'c', 'd', 'e'};
  OAK_CHECK_EQ(ring.write(data.data(), data.size()), 4u);

  // Full now: another write takes nothing, and says so rather than blocking.
  OAK_CHECK_EQ(ring.write(data.data(), data.size()), 0u);
}

OAK_TEST(ring_buffer_wraps_around) {
  RingBuffer ring(8);
  const auto first = bytesOf("abcd");
  const auto second = bytesOf("efgh");

  std::vector<std::uint8_t> out(4, 0);

  OAK_CHECK_EQ(ring.write(first.data(), first.size()), 4u);
  OAK_CHECK_EQ(ring.read(out.data(), out.size()), 4u);

  OAK_CHECK_EQ(ring.write(second.data(), second.size()), 4u);
  // This one straddles the end of the storage, which is exactly where a naive
  // implementation writes past the buffer.
  OAK_CHECK_EQ(ring.write(first.data(), first.size()), 4u);

  OAK_CHECK_EQ(ring.read(out.data(), out.size()), 4u);
  OAK_CHECK(out == second);
  OAK_CHECK_EQ(ring.read(out.data(), out.size()), 4u);
  OAK_CHECK(out == first);
}

OAK_TEST(ring_buffer_takes_part_of_what_it_is_offered) {
  RingBuffer ring(6);
  const auto data = bytesOf("abcdefghij");

  // Six usable bytes, ten offered: it takes six and reports the shortfall by
  // returning less. That return value is the whole contract.
  OAK_CHECK_EQ(ring.write(data.data(), data.size()), 6u);

  std::vector<std::uint8_t> out(6, 0);
  OAK_CHECK_EQ(ring.read(out.data(), out.size()), 6u);
  OAK_CHECK_EQ(textOf(out), std::string("abcdef"));
}

OAK_TEST(ring_buffer_ignores_nothing_at_all) {
  RingBuffer ring(4);
  OAK_CHECK_EQ(ring.write(nullptr, 4), 0u);
  OAK_CHECK_EQ(ring.write(reinterpret_cast<const std::uint8_t*>("x"), 0), 0u);

  std::vector<std::uint8_t> out(4, 0);
  OAK_CHECK_EQ(ring.read(nullptr, 4), 0u);
  OAK_CHECK_EQ(ring.read(out.data(), 0), 0u);
  OAK_CHECK_EQ(ring.read(out.data(), out.size()), 0u);
}

OAK_TEST(ring_buffer_moves_one_byte_at_a_time_across_the_wrap) {
  RingBuffer ring(4);
  std::uint8_t value = 0;

  for (int round = 0; round < 24; ++round) {
    const auto written = static_cast<std::uint8_t>(round);
    OAK_CHECK_EQ(ring.write(&written, 1), 1u);
    OAK_CHECK_EQ(ring.read(&value, 1), 1u);
    OAK_CHECK_EQ(value, written);
  }

  OAK_CHECK(ring.empty());
}

// ---------------------------------------------------------------- the channel

OAK_TEST(bulk_channel_forwards_what_the_engine_offers) {
  auto transport = std::make_unique<CollectingTransport>();
  CollectingTransport* observer = transport.get();

  BulkChannel channel(std::move(transport));

  const auto data = bytesOf("audio-audio-audio");
  OAK_CHECK_EQ(BulkChannel::receive(data.data(), data.size(), channel.context()),
               data.size());

  OAK_CHECK(waitFor([observer, &data] { return observer->collected().size() == data.size(); },
                    std::chrono::milliseconds(2000)));
  OAK_CHECK(observer->collected() == data);
  OAK_CHECK_EQ(channel.bytesForwarded(), static_cast<std::uint64_t>(data.size()));
  OAK_CHECK_EQ(channel.bytesDropped(), 0u);

  OAK_CHECK(channel.stop());
}

OAK_TEST(bulk_channel_refuses_what_does_not_fit_and_counts_it) {
  auto transport = std::make_unique<CollectingTransport>();
  BulkChannel channel(std::move(transport));

  // Far more than the queue holds, offered in one call.
  const std::vector<std::uint8_t> huge(1024 * 1024, 0x7f);
  const std::size_t taken =
      BulkChannel::receive(huge.data(), huge.size(), channel.context());

  // Refused rather than queued and rather than blocked: an engine's audio
  // thread must not be made to wait, and an unbounded queue would turn a slow
  // host into growing latency.
  OAK_CHECK(taken < huge.size());
  OAK_CHECK(taken > 0);
  OAK_CHECK_EQ(channel.bytesDropped(),
               static_cast<std::uint64_t>(huge.size() - taken));

  OAK_CHECK(channel.stop());
}

OAK_TEST(bulk_channel_ignores_a_missing_context) {
  const std::uint8_t byte = 1;
  OAK_CHECK_EQ(BulkChannel::receive(&byte, 1, nullptr), 0u);
  OAK_CHECK_EQ(BulkChannel::receive(nullptr, 1, nullptr), 0u);
}

OAK_TEST(bulk_channel_forwards_across_several_offerings) {
  auto transport = std::make_unique<CollectingTransport>();
  CollectingTransport* observer = transport.get();

  BulkChannel channel(std::move(transport));

  // Offered in pieces, the way an engine would: one buffer per audio callback.
  std::string expected;
  for (int block = 0; block < 8; ++block) {
    const std::string piece = "block-" + std::to_string(block) + ";";
    expected += piece;
    const auto bytes = bytesOf(piece);
    OAK_CHECK_EQ(BulkChannel::receive(bytes.data(), bytes.size(), channel.context()),
                 bytes.size());
  }

  OAK_CHECK(waitFor(
      [observer, &expected] { return observer->collected().size() == expected.size(); },
      std::chrono::milliseconds(2000)));
  OAK_CHECK_EQ(textOf(observer->collected()), expected);

  OAK_CHECK(channel.stop());
}
