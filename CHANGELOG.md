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

- `pubspec.yaml` no longer declares a `plugin:` section and no longer depends
  on the Flutter SDK. The host-side client is transport and protocol only,
  which also lets it be tested with plain `dart test`.

- Removed `src/shim/` (the build-time C shim contract). Replaced by
  `src/relay/` with `protocol/`, `transport/`, `discovery/` and `binding/`
  subdivisions matching the five lifecycle stages.

### Added

- `docs/RELAY_PROTOCOL.md` — the normative relay protocol: framing, transport
  options, negotiation, operations, reason codes, redaction rules, and the
  version-compatibility strategy.
- CI hygiene guard now also checks that no vendor name, version string or
  absolute path has been committed into source or documentation.

### Removed

- Build-time engine linkage and its FFI surface, along with every constant
  that encoded a specific version. Anything hardcoded would have been a
  version this project was silently married to.

### Notes

- TODO(protocol): freeze protocol revision 1 prior to implementation.
- TODO(relay): transport implementations (unix socket / named pipe / stdio).
- TODO(relay): discovery from neutral signals, with conservative class mapping.
- TODO(relay): dynamic binding layer.
- TODO(client): Dart host-side client.
- TODO(example): reference host demonstrating connect → caps → open → post.

## [0.1.0] - 2026-09-18

### Added

- Initial repository structure. No functional integration yet.
