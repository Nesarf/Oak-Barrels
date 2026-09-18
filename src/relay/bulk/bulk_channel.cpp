#include "relay/bulk/bulk_channel.hpp"

#include <algorithm>
#include <chrono>
#include <utility>
#include <vector>

namespace oak::relay::bulk {
namespace {

/// How much goes to the host in one write.
constexpr std::size_t kDrainBytes = 16u * 1024u;

/// How long the queue is. About a third of a second of stereo float audio at
/// 48 kHz: long enough to ride out scheduling jitter, short enough that a host
/// which has stopped reading is noticed rather than buffered for minutes.
constexpr std::size_t kQueueBytes = 128u * 1024u;

/// How long the worker sleeps when the queue is empty.
constexpr auto kIdlePause = std::chrono::milliseconds(2);

/// How long `stop` waits for the worker before giving up on it.
constexpr auto kStopGrace = std::chrono::milliseconds(250);

// The sink runs on an audio thread, so its bookkeeping has to be genuinely
// lock-free rather than usually lock-free. On every platform this project
// targets these hold; asserting it turns a platform assumption into a build
// error on one where it would not.
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "the audio sink counts bytes with atomics and must never lock");

}  // namespace

BulkChannel::BulkChannel(std::unique_ptr<transport::Transport> endpoint)
    : endpoint_(std::move(endpoint)), ring_(kQueueBytes) {
  worker_ = std::thread([this] { run(); });
}

BulkChannel::~BulkChannel() {
  if (worker_.joinable()) {
    worker_.detach();
  }
}

std::size_t BulkChannel::receive(const void* data, std::size_t bytes, void* context) {
  if (context == nullptr || data == nullptr || bytes == 0) return 0;

  auto* channel = static_cast<BulkChannel*>(context);
  const auto* raw = static_cast<const std::uint8_t*>(data);

  const std::size_t taken = channel->ring_.write(raw, bytes);
  if (taken < bytes) {
    // Refused rather than queued. Blocking here would stall the engine's audio
    // thread, and an unbounded queue would turn a slow host into growing
    // latency; counting it is the honest third option.
    channel->dropped_.fetch_add(bytes - taken, std::memory_order_relaxed);
  }
  return taken;
}

void BulkChannel::run() {
  std::vector<std::uint8_t> buffer(kDrainBytes);

  while (!stopping_.load(std::memory_order_relaxed)) {
    const std::size_t taken = ring_.read(buffer.data(), buffer.size());
    if (taken == 0) {
      std::this_thread::sleep_for(kIdlePause);
      continue;
    }

    const std::vector<std::uint8_t> chunk(
        buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(taken));
    if (!endpoint_->write(chunk)) break;

    forwarded_.fetch_add(taken, std::memory_order_relaxed);
  }

  markFinished();
}

void BulkChannel::markFinished() {
  {
    const std::lock_guard<std::mutex> lock(finishedMutex_);
    finished_ = true;
  }
  finishedCondition_.notify_all();
}

bool BulkChannel::stop() {
  stopping_.store(true, std::memory_order_relaxed);

  if (!worker_.joinable()) return true;

  bool finished = false;
  {
    std::unique_lock<std::mutex> lock(finishedMutex_);
    finished = finishedCondition_.wait_for(lock, kStopGrace, [this] { return finished_; });
  }

  if (finished) {
    worker_.join();
    return true;
  }

  // Still inside a write to a host that stopped reading. Detaching is the only
  // safe option left: joining would hang the station, and killing a thread is
  // not a thing.
  worker_.detach();
  return false;
}

}  // namespace oak::relay::bulk
