# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- **Repositioned the project from an integration plugin to a neutral relay
  station.** The earlier design linked an audio engine at build time and
  exposed it to a host through FFI. That design is gone, for a structural
  reason: a build-time link cannot be version-independent, and version
  independence is the point.

  The relay now:
  - belongs to neither the host framework nor the audio engine,
  - links nothing at build time and resolves everything at run time,
  - expresses compatibility as **classes** rather than version numbers,
  - keeps discovered facts in memory, redacted by default.

- **The project declares no applicable target.** It is described by what it
  does rather than by a pairing it was built for, and it names none. A relay
  that announced "this is for A and B" would become an integration for A and B
  the moment those names were written down, and would be quietly wrong the
  moment either of them moved.

- **Tracked text is ASCII only**, enforced by CI. A compiler that decodes
  source using the machine's code page can build the same file differently in
  different regions. The substitution table is in `CONTRIBUTING.md`.

- The station claims **only** the classes it proved, and says so after having
  looked. A profile is satisfied when every symbol it names resolves; until
  then, no class is offered at all. Recognising a container format is not
  knowing a calling convention, so a format alone never yields a class.
  Under-claiming means a host does less; over-claiming crashes on a user's
  machine.

- **A5 added to the protocol's axioms: every engine is assumed unlicensed.**
  Nothing in discovery, probing or binding may depend on a licensed capability,
  and a licence-gated capability is reported as absent rather than assumed
  present. An unverifiable capability is an absent one, which is the same
  conservatism the class mapping already required.

- `pubspec.yaml` no longer declares a `plugin:` section and no longer depends
  on the Flutter SDK. The host-side client is transport and protocol only,
  which also lets it be tested with plain `dart test`.

- Removed `src/shim/` (the build-time C shim contract). Replaced by
  `src/relay/` with `protocol/`, `transport/`, `discovery/` and `binding/`
  subdivisions matching the five lifecycle stages.

### Added

- `docs/RELAY_PROTOCOL.md` -- the normative relay protocol: framing, transport
  options, negotiation, operations, reason codes, redaction rules, and the
  version-compatibility strategy.
- **The native relay station** (`src/relay/`): frame codec, a small
  dependency-free JSON value type, the session state machine, and a stdio
  transport. Built with CMake, with no third-party build dependency.
- **The host-side Dart client** (`lib/`): transport abstraction, a stdio
  transport that spawns a station, and a paired-request client over the wire
  protocol.
- `Relay.diagnostics()` on the host client, so the diagnostics switch of
  protocol section 8 is reachable and not merely specified.
- Native test suite (`tests/`, 85 cases) covering the frame codec, the JSON
  type, module identification, the scanner, and every session rule, alongside
  the Dart suite.
- An end-to-end suite that spawns the real station binary and speaks the real
  protocol over real pipes. It skips when no station has been built; CI builds
  one first, because two fakes can agree about a protocol neither end
  implements.
- A CI job that builds the station, runs the native tests, and then runs the
  end-to-end suite against the binary it just produced.
- CI hygiene guard now also checks that no vendor name, version string,
  absolute path or non-ASCII character has been committed.
- `example/reference_host.dart`, a runnable host that walks negotiate ->
  capabilities -> open -> shutdown and degrades gracefully when the station
  will not serve it, which is what every host has to do anyway.
- **Discovery, in the only form that is honest without a probe.** Candidate
  module files are identified by their operating-system container format (PE,
  ELF, Mach-O): public, stable, and owned by nobody. They are enumerated only
  under directories the host nominates with `--search-root`, so the station
  invents no search locations and, with no root nominated, never touches the
  filesystem at all.
- Diagnostics report how many files were examined and how many looked like
  modules. Counts, never paths: a path is a discovered fact and stays where it
  was found.
- **The dynamic binding layer** (`src/relay/binding/`): mapping a module and
  resolving a name inside it, and nothing else. It calls nothing, because
  resolving a symbol says a name exists and says nothing about what happens when
  it is called.
- **Probe profiles** (`src/relay/probe/`): the host describes, as data, which
  symbols a usable engine must export and how this relay will call them. The
  vocabulary of calling shapes is deliberately two entries wide, and contains
  only the shapes the protocol can actually deliver.
- **Probing**, and a backend that offers only what it proved. A profile is
  satisfied when every symbol it names resolves; every satisfied profile is
  bound, so one module offering several capabilities reports all of them.
- `--probe-profile`, and `--listen` / `--pipe` for the two non-stdio transports.
- A unix domain socket transport (station and Dart client) and a named pipe
  transport (station). Each serves exactly one peer and refuses a second, so a
  host is never left guessing why nothing answered.
- A stand-in engine in the test tree, and a fixture that proves the whole chain
  end to end: scan, recognise a container format, load, resolve, bind, call, and
  observe that the call landed. Nothing links the fixture, so the load path is
  genuinely exercised rather than bypassed by the linker.
- **The bulk audio channel of protocol section 7** (`src/relay/bulk/`), on a
  second transport, declared as `engine.bulk.pcm`, and offered only while a
  channel is genuinely running. The engine is handed a sink and calls it; the
  sink returns how many bytes it took, never blocks, and never allocates,
  because it runs on whatever thread the engine chose. What it refused is
  counted rather than hidden.
- The third calling shape, `sink`, which is the only one called *back*. The
  vocabulary is still short enough to hold in your head: a value, a name, and
  somewhere to put audio are the only things the protocol can actually deliver.
- `--bulk <name>` on the station, for the endpoint a host nominates.
- A bounded, lock-free single-producer queue, tested on its own because it is
  the only thing an engine's audio thread ever touches.
- A named pipe transport for the Dart host, reached through `dart:ffi` with
  no package added: the allocation and UTF-16 conversion that a helper package
  would supply are short against `LocalAlloc` and the fact that a Dart string is
  already UTF-16. Reading runs on its own isolate and asks whether data is
  waiting rather than sitting in a blocking read, because a worker blocked
  inside a system call cannot be killed and a read issued before the far end has
  finished connecting fails instead of waiting.

### Removed

- Build-time engine linkage and its FFI surface, along with every constant
  that encoded a specific version. Anything hardcoded would have been a
  version this project was silently married to.
- `README.zh-CN.md`. The repository is English and ASCII throughout, so a
  translation was the one file the encoding rule could not cover.

### Notes

- The station binds whatever the host describes, and cannot invent a
  description of its own. Until a profile says what to look for, no engine can
  be found -- deliberately, and that is axiom A2 talking rather than an
  unfinished stage.
- There is no outstanding protocol work. Section 7's channel was the last item,
  and it needed no new revision: the `bulk` field and the `engine.bulk.pcm`
  class name were already in `CAPS_REPLY`, so this work only gave them something
  true to report.

## [0.1.0] - 2026-09-18

### Added

- Initial repository structure. No functional integration yet.
