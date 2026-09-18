// Carrying audio from a bound engine to one host, off the control path.
//
// Protocol section 7 is specific about why this is a separate transport and not
// more messages: a host that stops reading its audio must stall its audio and
// nothing else. If these bytes travelled over the control pipe, a slow consumer
// would stop the relay from answering anything.
//
// Three requirements come from the same section, and each is met structurally
// rather than by remembering to:
//
//   * a separate transport -- this class owns one and knows nothing else;
//   * declared as a capability class -- the session only offers
//     `engine.bulk.pcm` while one of these is running;
//   * silent about formats it cannot honour -- the channel never describes what
//     the bytes are. It moves them. The engine and the host agree on a format
//     between themselves, and a relay that never made a claim cannot make a
//     false one.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "relay/bulk/ring_buffer.hpp"
#include "relay/bulk/sink.hpp"
#include "relay/transport/transport.hpp"

namespace oak::relay::bulk {

class BulkChannel {
 public:
  /// Takes an already-connected endpoint and starts forwarding into it.
  ///
  /// The peer is accepted by the caller, before this exists. Accepting here
  /// would mean a worker that can be blocked in a call nothing can interrupt,
  /// and a station that cannot be shut down while a host declines to connect.
  explicit BulkChannel(std::unique_ptr<transport::Transport> endpoint);
  ~BulkChannel();

  BulkChannel(const BulkChannel&) = delete;
  BulkChannel& operator=(const BulkChannel&) = delete;

  /// The sink handed to an engine. Safe to call from an audio thread.
  static std::size_t receive(const void* data, std::size_t bytes, void* context);

  Sink sink() const { return &BulkChannel::receive; }

  /// The context to hand to an engine alongside the sink.
  void* context() { return this; }

  /// Bytes that reached the host.
  std::uint64_t bytesForwarded() const {
    return forwarded_.load(std::memory_order_relaxed);
  }

  /// Bytes an engine offered that did not fit, and were therefore refused.
  ///
  /// Counted rather than hidden: a nonzero value means the host is behind, and
  /// that is something an operator needs to be able to see.
  std::uint64_t bytesDropped() const {
    return dropped_.load(std::memory_order_relaxed);
  }

  /// Asks the worker to stop, and waits briefly for it.
  ///
  /// Returns false when the worker did not stop in time -- which happens when
  /// it is inside a write to a host that has stopped reading. In that case the
  /// caller is expected to leak this object rather than destroy it: freeing
  /// memory a live thread may still touch is worse than not freeing it, and the
  /// process is on its way out anyway.
  bool stop();

 private:
  void run();
  void markFinished();

  std::unique_ptr<transport::Transport> endpoint_;
  RingBuffer ring_;

  std::thread worker_;
  std::atomic<bool> stopping_{false};

  std::mutex finishedMutex_;
  std::condition_variable finishedCondition_;
  bool finished_ = false;

  std::atomic<std::uint64_t> forwarded_{0};
  std::atomic<std::uint64_t> dropped_{0};
};

}  // namespace oak::relay::bulk
