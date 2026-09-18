// The shape the relay hands to an engine that produces audio.
//
// This is the third and last entry in the relay's vocabulary of calling shapes,
// and it is the only one that is called *back*. The other two go outwards, from
// the relay into the engine; this one comes back, from the engine into the
// relay, on whatever thread the engine chooses.
//
// That is why the signature is what it is. A sink must be safe to call from an
// audio thread, which rules out blocking, allocating and locking. So it copies
// what fits into a bounded queue and returns how much it took, leaving the
// decision about the remainder to the engine -- which is the only party that
// knows whether dropping audio or stopping is worse.
#pragma once

#include <cstddef>

namespace oak::relay::bulk {

/// A place for an engine to put audio. Called on the engine's thread.
///
/// Returns the number of bytes accepted, which may be fewer than offered. It
/// never blocks and never allocates.
using Sink = std::size_t (*)(const void* data, std::size_t bytes, void* context);

/// What the relay calls to install a [Sink] into an engine.
///
/// The engine keeps both arguments and calls the sink when it has audio. A
/// conforming engine must also tolerate the sink returning a short count, and
/// must not call it after the relay has gone.
using RegisterFunction = void (*)(Sink sink, void* context);

}  // namespace oak::relay::bulk
