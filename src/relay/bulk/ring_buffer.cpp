#include "relay/bulk/ring_buffer.hpp"

#include <algorithm>
#include <cstring>

namespace oak::relay::bulk {

RingBuffer::RingBuffer(std::size_t capacity)
    : storage_(capacity + 1, 0), slots_(capacity + 1), usable_(capacity) {}

std::size_t RingBuffer::write(const std::uint8_t* data, std::size_t bytes) {
  if (data == nullptr || bytes == 0) return 0;

  const std::size_t head = head_.load(std::memory_order_relaxed);
  // Acquire: the consumer published this after reading bytes out, and the space
  // it freed is only ours to use once we have seen that.
  const std::size_t tail = tail_.load(std::memory_order_acquire);

  const std::size_t used = (head + slots_ - tail) % slots_;
  const std::size_t available = slots_ - 1 - used;
  const std::size_t taken = std::min(bytes, available);
  if (taken == 0) return 0;

  // At most two copies, because the free region may wrap.
  const std::size_t firstRun = std::min(taken, slots_ - head);
  std::memcpy(storage_.data() + head, data, firstRun);
  if (taken > firstRun) {
    std::memcpy(storage_.data(), data + firstRun, taken - firstRun);
  }

  // Release: the bytes above must be visible before the index that includes
  // them, or the consumer could read slots that were not written yet.
  head_.store((head + taken) % slots_, std::memory_order_release);
  return taken;
}

std::size_t RingBuffer::read(std::uint8_t* out, std::size_t capacity) {
  if (out == nullptr || capacity == 0) return 0;

  const std::size_t tail = tail_.load(std::memory_order_relaxed);
  const std::size_t head = head_.load(std::memory_order_acquire);

  const std::size_t used = (head + slots_ - tail) % slots_;
  const std::size_t taken = std::min(capacity, used);
  if (taken == 0) return 0;

  const std::size_t firstRun = std::min(taken, slots_ - tail);
  std::memcpy(out, storage_.data() + tail, firstRun);
  if (taken > firstRun) {
    std::memcpy(out + firstRun, storage_.data(), taken - firstRun);
  }

  tail_.store((tail + taken) % slots_, std::memory_order_release);
  return taken;
}

std::size_t RingBuffer::size() const {
  const std::size_t head = head_.load(std::memory_order_acquire);
  const std::size_t tail = tail_.load(std::memory_order_acquire);
  return (head + slots_ - tail) % slots_;
}

}  // namespace oak::relay::bulk
