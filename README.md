# oak_barrels

> A **neutral relay** between a host application (Flutter or anything else that
> can open a local pipe) and whatever audio engine happens to be installed on
> the machine.

[![CI](https://github.com/Nesarf/oak-barrels/actions/workflows/ci.yml/badge.svg)](https://github.com/Nesarf/oak-barrels/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

---

## What this is

A **relay station** — a process that sits between two sides and lets them talk
without either side knowing the other's internals.

```
┌──────────────────┐        ┌────────────────────┐        ┌──────────────────┐
│  Host app        │  pipe  │   Relay station    │ dlopen │  Audio engine    │
│  (Flutter, …)    │◀──────▶│   (this project)   │◀──────▶│  (installed by   │
│                  │        │                    │        │   the user)      │
└──────────────────┘        └────────────────────┘        └──────────────────┘
```

Three properties define it:

**1. It belongs to neither side.**
This is not "the Wwise plugin for Flutter", nor "the Flutter binding for
Wwise". It installs alongside neither, depends on neither at build time, and
is not distributed with either. It is a thing that exists on its own.

**2. It links nothing at build time.**
No audio-engine headers, no audio-engine libraries, no host-framework
libraries. Everything on both sides is discovered and bound **at run time**.
That is what makes version independence possible instead of aspirational.

**3. It discovers, then adapts.**
On startup it surveys what is actually present on the machine — engine
installations, their versions, the host runtime's capabilities — and selects a
compatible way to talk to them. Nothing is hardcoded, because anything
hardcoded would be a version this project is silently married to.

## What this is not

- Not affiliated with, endorsed by, or sponsored by any audio middleware vendor
  or any UI framework vendor.
- Not a redistribution channel. It ships no third-party binaries.
- Not a fork, wrapper, or repackaging of anybody's SDK.
- Not a build-time dependency of anything.

## Design consequences of being a relay

Because the station links nothing, it cannot call engine functions directly at
compile time. It resolves them at run time instead:

```
discover  →  probe  →  negotiate  →  bind  →  relay
   │           │          │           │         │
   │           │          │           │         └─ forward calls, return results
   │           │          │           └─ resolve symbols dynamically
   │           │          └─ agree on a protocol revision both sides can speak
   │           └─ determine versions & capabilities without assuming any
   └─ find candidate installations from neutral, generic signals
```

Each stage is specified in [`docs/RELAY_PROTOCOL.md`](docs/RELAY_PROTOCOL.md).

### Why not just link directly?

Linking directly is simpler **once** — and then every release of either side
forces a rebuild of this project, and every user must match the exact versions
the maintainer happened to have. A relay trades a little run-time complexity
for the property that actually matters here: **it keeps working when the two
sides move independently.**

## Quiet by default

The station is built to avoid leaving identifying traces:

- No vendor names, versions, paths or build fingerprints are compiled into the
  relay binary.
- Version and path information discovered at run time stays in memory. It is
  not written to disk, not logged by default, and not echoed back over the pipe
  unless a diagnostic mode is explicitly enabled.
- Diagnostic output, when enabled, is redacted by default: paths are reduced to
  opaque identifiers and version strings are reduced to compatibility classes
  rather than exact builds.
- The repository contains no machine-specific configuration and no recorded
  environment details. See [CONTRIBUTING.md](CONTRIBUTING.md) for the rule that
  keeps it that way.

This matters because a tool that reports exactly which versions you run is a
tool that fingerprints you. The relay should be able to say "compatible" or
"incompatible" without saying *what* it found.

## Status

**Pre-alpha. Nothing functional yet.** The interface contract is being fixed
before implementation, because a relay's protocol is the expensive thing to
change later.

| Piece | State |
| --- | --- |
| Repository scaffolding, protocol docs, CI | ✅ |
| Relay protocol specification | 🚧 in progress |
| Discovery / probe / negotiation logic | ⏳ planned |
| Dynamic binding layer | ⏳ planned |
| Pipe transport (host side) | ⏳ planned |
| Host-side Dart client | ⏳ planned |
| Reference host example | ⏳ planned |

## Interface sketch

Illustrative only — the wire protocol is the authority, not this snippet.
Note that **no version number appears anywhere**, by design.

```dart
// Host side: open a relay and talk to it without knowing what is behind it.
final relay = await Relay.connect();

final caps  = await relay.capabilities();   // compatibility classes, not versions
final voice = await relay.open('emitter');

await relay.post('event.play',   target: voice, args: {'name': 'ice_drop'});
await relay.set ('param.intensity', value: 0.82, target: voice);
await relay.close(voice);

await relay.shutdown();
```

## Building

The relay has **no third-party build dependencies**. It does not read a
vendor SDK path, because it does not link a vendor SDK.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The host-side client is pure Dart and needs no native toolchain:

```bash
dart pub get
dart test
```

## Non-goals

- Reimplementing an audio engine. The relay carries instructions, it does not
  synthesise sound.
- Shipping, bundling, or downloading anybody's binaries.
- Being the recommended path for a specific vendor pairing. It has no
  recommendation to make; it has a pipe to offer.

## License

MIT — see [LICENSE](LICENSE).

---

*中文版见 [README.zh-CN.md](README.zh-CN.md)。*
