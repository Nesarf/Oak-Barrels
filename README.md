# oak_barrels

> A **neutral relay** between a host application and whichever audio engine
> happens to be installed on the machine.
>
> It declares **no applicable target**. It names none, and it is built for no
> particular pairing -- see [What this is](#what-this-is).

[![CI](https://github.com/Nesarf/Oak-Barrels/actions/workflows/ci.yml/badge.svg)](https://github.com/Nesarf/Oak-Barrels/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

---

## What this is

A **relay station** -- a process that sits between two sides and lets them talk
without either side knowing the other's internals.

```
+------------------+        +--------------------+        +------------------+
|  Host app        |  pipe  |   Relay station    | dlopen |  Audio engine    |
|  (any process    |<------>|   (this project)   |<------>|  (installed by   |
|   with a pipe)   |        |                    |        |   the user)      |
+------------------+        +--------------------+        +------------------+
```

Four properties define it:

**1. It belongs to neither side.**
It is not a plugin *for* anything, and not a binding *to* anything. It installs
alongside neither side, depends on neither at build time, and is not
distributed with either. It is a thing that exists on its own.

**2. It names no applicable target.**
A relay that announced "this is for A and B" would become an integration for A
and B the moment those names were written down -- and would be quietly wrong the
moment either of them moved. So it does not write them down. The relay is
defined by *what it does* -- carry control between a host and an engine --
not by *who it is for*. That is a deliberate absence, not an unfinished
sentence.

**3. It links nothing at build time.**
No audio-engine headers, no audio-engine libraries, no host-framework
libraries. Everything on both sides is discovered and bound **at run time**.
That is what makes version independence possible instead of aspirational.

**4. It discovers, then adapts.**
On startup it surveys what is actually present on the machine -- candidate
installations, their capabilities, the host runtime's shape -- and selects a
compatible way to talk to them. Nothing is hardcoded, because anything
hardcoded would be a version this project is silently married to.

## What this is not

- Not affiliated with, endorsed by, or sponsored by any audio middleware vendor
  or any UI framework vendor.
- Not declared compatible with, or intended for, any named product. It has no
  target pairing to announce.
- Not a redistribution channel. It ships no third-party binaries.
- Not a fork, wrapper, or repackaging of anybody's SDK.
- Not a build-time dependency of anything.

## Design consequences of being a relay

Because the station links nothing, it cannot call engine functions directly at
compile time. It resolves them at run time instead:

```
discover  ->  probe  ->  negotiate  ->  bind  ->  relay
   |           |          |           |         |
   |           |          |           |         +- forward calls, return results
   |           |          |           +- resolve symbols dynamically
   |           |          +- agree on a protocol revision both sides can speak
   |           +- determine capabilities without assuming any
   +- find candidate installations from neutral, generic signals
```

Each stage is specified in [`docs/RELAY_PROTOCOL.md`](docs/RELAY_PROTOCOL.md).

### Why not just link directly?

Linking directly is simpler **once** -- and then every release of either side
forces a rebuild of this project, and every user must match the exact versions
the maintainer happened to have. A relay trades a little run-time complexity
for the property that actually matters here: **it keeps working when the two
sides move independently.**

## Quiet by default

The station is built to avoid leaving identifying traces:

- No vendor names, versions, paths or build fingerprints are compiled into the
  relay binary.
- Facts discovered at run time stay in memory. They are not written to disk,
  not logged by default, and not echoed back over the pipe unless a diagnostic
  mode is explicitly enabled.
- Diagnostic output, when enabled, is redacted by default: paths are reduced to
  opaque identifiers and version strings are reduced to compatibility classes
  rather than exact builds.
- Identification is a **separate switch**, off by default and off independently
  of diagnostics. A host that never sets it can run for years without the relay
  ever telling it what it is talking to -- and that is the intended posture.
- The repository contains no machine-specific configuration and no recorded
  environment details. See [CONTRIBUTING.md](CONTRIBUTING.md) for the rule that
  keeps it that way.

This matters because a tool that reports exactly which versions you run is a
tool that fingerprints you. The relay should be able to say "compatible" or
"incompatible" without saying *what* it found.

## Status

**Pre-alpha, and now genuinely useful in a narrow way.** A station binds an
engine the host describes, and offers exactly the classes it proved.

A station started with no nominated root and no profile examines nothing and
refuses every `OPEN` with `NO_ENGINE` -- the conservative answer, and the right
one for a relay that has been told nothing. Given a root and a profile, it
examines what it finds, resolves the entry points that profile names, and offers
only the classes whose every symbol resolved. Refusing is the correct
behaviour: advertising a compatibility class it cannot honour is exactly the
over-claiming that protocol section 10 forbids.

| Piece | State |
| --- | --- |
| Repository scaffolding, protocol docs, CI | [x] |
| Relay protocol specification | [x] revision 1, open to argument |
| Frame codec, session state machine (native) | [x] |
| stdio transport, both sides | [x] |
| unix socket transport, station and Dart client | [x] |
| named pipe transport, station | [x] |
| Host-side client (Dart) | [x] |
| Discovery from container format, host-nominated | [x] |
| Probe profiles and dynamic binding | [x] |
| Reference host example | [x] |
| Named pipe client for the Dart host | [x] via dart:ffi, with no package added |
| Bulk audio channel (protocol section 7) | [x] a second transport, with a sink installed into the engine |

## Telling a station what to do

The station is configured entirely by the host, at launch. Nothing below is
baked into the build.

| Flag | What it is for |
| --- | --- |
| *(none)* | serve one session over standard input and output |
| `--listen <path>` | serve one connection on a unix domain socket |
| `--pipe <name>` | serve one client on a named pipe |
| `--bulk <name>` | also serve the audio channel of section 7 on a second endpoint |
| `--search-root <dir>` | a directory to examine. Repeatable. |
| `--probe-profile <file>` | how to talk to whatever is found there |

Both the search location and the symbol names arrive at run time, from the host.
That is how a station binds an engine it has never heard of, and why it can do
so while shipping knowledge of no product at all.

## Interface

The host-side client, as it actually is. Note that **no version number appears
anywhere**, by design.

```dart
import 'package:oak_barrels/oak_barrels.dart';

// Spawn a station and talk to it without knowing what is behind it.
final relay = await Relay.spawn('/path/to/oak-barrels');

final revision = await relay.negotiate();    // which revision both sides speak
final caps     = await relay.capabilities(); // compatibility classes, not versions

final emitter = await relay.open('emitter', name: 'ui');
await relay.post(emitter, 'ice_drop', args: {'intensity': 0.8});
await relay.set (emitter, 'intensity', 0.82);
await relay.close(emitter);

await relay.shutdown();
```

The wire protocol is the authority, not this snippet -- see
[`docs/RELAY_PROTOCOL.md`](docs/RELAY_PROTOCOL.md).

## Building

The relay has **no third-party build dependencies**. It does not read a vendor
SDK path, because it does not link a vendor SDK.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
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

MIT -- see [LICENSE](LICENSE).
