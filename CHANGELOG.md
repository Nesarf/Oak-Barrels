# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Repository scaffolding: `README.md`, `README.zh-CN.md`, `LICENSE`,
  `CHANGELOG.md`, `CONTRIBUTING.md`, `analysis_options.yaml`, CI workflow.
- Architecture documented for the chain
  **Dart → `dart:ffi` → C shim → Wwise sound engine**.
- Rationale recorded for why a flat C shim is used instead of binding the
  engine's name-mangled C++ symbols directly.
- Platform library matrix documented (Windows dynamic + static, Linux and
  Android static).

### Notes

- TODO(shim): implement `hc_wwise_*` C surface in `src/shim/`.
- TODO(ffi): generate Dart bindings with `ffigen` into `lib/src/ffi/`.
- TODO(build): CMake resolution of `WWISE_SDK_ROOT` per platform.
- TODO(example): minimal example app demonstrating init → bank → event.
- TODO(platform): Linux and Android build validation.

## [0.1.0] - 2026-09-18

### Added

- Initial repository structure. No functional integration yet.
