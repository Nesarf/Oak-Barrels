// A bounded byte queue safe to write from an audio thread.
//
// Single producer, single consumer, no locks, no allocation after construction.
// Writing never blocks: it takes what fits and reports how much that was, which
// is the only honest thing a queue can do when the alternative is stalling the
// thread that is producing sound.
//
// The lock-free part is small enough to state exactly. The producer owns `head`
// and only reads `tail`; the consumer owns `tail` and only reads `head`. Each
// publishes its own index with a release store so that the bytes it wrote are
// visible before the index that mentions them, and reads the other with an
// acquire load for the same reason in reverse.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace oak::relay::bulk {

class RingBuffer {
 public:
  /// Creates a queue holding [capacity] usable bytes.
  ///
  /// One extra slot is allocated and deliberately left unused: without it, a
  /// full queue and an empty one look identical.
  explicit RingBuffer(std::size_t capacity);

  RingBuffer(const RingBuffer&) = delete;
  RingBuffer& operator=(const RingBuffer&) = delete;

  /// Copies as much of [data] as fits. Returns the number of bytes taken.
  ///
  /// For one producer thread only.
  std::size_t write(const std::uint8_t* data, std::size_t bytes);

  /// Copies out up to [capacity] bytes. Returns the number of bytes taken.
  ///
  /// For one consumer thread only.
  std::size_t read(std::uint8_t* out, std::size_t capacity);

  /// Bytes available to read.
  std::size_t size() const;

  std::size_t capacity() const { return usable_; }
  bool empty() const { return size() == 0; }

 private:
  std::vector<std::uint8_t> storage_;
  std::size_t slots_;   ///< storage_.size(), which is usable_ + 1.
  std::size_t usable_;  ///< What a caller may actually queue.

  std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> tail_{0};
};

}  // namespace oak::relay::bulk
