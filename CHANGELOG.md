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

- The station still claims no compatibility classes, and now says so after
  having looked. Recognising a container format is not knowing a calling
  convention, so it reports what it examined and offers nothing. Under-claiming
  means a host does less; over-claiming crashes on a user's machine.

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

### Removed

- Build-time engine linkage and its FFI surface, along with every constant
  that encoded a specific version. Anything hardcoded would have been a
  version this project was silently married to.
- `README.zh-CN.md`. The repository is English and ASCII throughout, so a
  translation was the one file the encoding rule could not cover.

### Notes

- The station currently runs with no engine backend. It therefore reports no
  capability classes and refuses every `OPEN` with `NO_ENGINE`. Refusing is the
  correct behaviour: advertising a class it cannot honour is exactly the
  over-claiming that protocol section 10 forbids.
- TODO(relay): probing, and the class mapping proper. Container formats are
  recognised today but no calling shape is, so no class is claimable yet.
- TODO(relay): dynamic binding layer.
- TODO(transport): unix domain socket and named pipe transports.

## [0.1.0] - 2026-09-18

### Added

- Initial repository structure. No functional integration yet.
