# flutter_wwise

> Unofficial **Audiokinetic Wwise** integration for **Flutter** — on Windows, Linux and Android.
> Built on `dart:ffi`. No plugin channel, no Unity, no Unreal.

[![CI](https://github.com/Nesarf/flutter-wwise/actions/workflows/ci.yml/badge.svg)](https://github.com/Nesarf/flutter-wwise/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Flutter](https://img.shields.io/badge/Flutter-%3E%3D3.22-blue.svg)](https://flutter.dev)

---

## Why this exists

Audiokinetic ships **official** Wwise integrations for **Unity** and **Unreal Engine**.
For **Flutter**, there is nothing — an audio middleware with an industry-standard
reputation, and no way to reach it from a Flutter app.

This package is that missing layer. It links the Wwise sound engine into a Flutter
application and exposes a small, typed Dart API over it:

```
Flutter (Dart)  ──dart:ffi──▶  C shim  ──▶  Wwise sound engine
```

The **C shim** exists because Wwise's API lives in C++ namespaces
(`AK::SoundEngine::PostEvent`, …) while `dart:ffi` can only call flat C symbols.

## Status

**Early. Pre-alpha. Not yet usable in production.**

| Piece | State |
| --- | --- |
| Repository scaffolding, docs, CI | ✅ |
| C shim surface (declared) | 🚧 in progress |
| Dart FFI bindings | 🚧 in progress |
| Windows build (CMake) | 🚧 in progress |
| Linux build (CMake) | ⏳ planned |
| Android build (NDK/CMake) | ⏳ planned |
| Example app | ⏳ planned |

## Platform support

| Platform | Wwise engine core | Provided as |
| --- | --- | --- |
| **Windows** x64 / ARM64 | `AkSoundEngineDLL.dll` + `AkSoundEngine.lib` | dynamic + static |
| **Linux** x64 / aarch64 | `libAkSoundEngine.a` | static |
| **Android** arm64-v8a / armeabi-v7a / x86_64 | `libAkSoundEngine.a` | static |

> ⚠️ **You must supply your own Wwise installation.** This repository contains
> **no** Wwise SDK files — no headers, no libraries, no tools. See
> [Wwise licensing](#wwise-licensing) below.

## Architecture

```
┌──────────────────────────────────────────────┐
│  Dart API            lib/flutter_wwise.dart  │
│  Wwise.init / loadBank / postEvent / setRtpc │
├──────────────────────────────────────────────┤
│  FFI bindings        lib/src/ffi/            │
├──────────────────────────────────────────────┤
│  C shim              src/shim/               │
│  hc_wwise_*  (flat C surface over C++ API)   │
├──────────────────────────────────────────────┤
│  Wwise sound engine  (your SDK, your license)│
└──────────────────────────────────────────────┘
```

**Why a shim and not raw FFI against the engine?**
Because the exported C++ symbols are name-mangled and namespace-qualified.
A thin `extern "C"` layer gives stable, version-tolerant symbols, keeps
`dart:ffi` bindings simple, and gives one place to absorb SDK differences
between Wwise versions.

## Installation

Not yet published to pub.dev. Once it is:

```yaml
dependencies:
  flutter_wwise: ^0.1.0
```

Until then, depend on it from Git:

```yaml
dependencies:
  flutter_wwise:
    git:
      url: https://github.com/Nesarf/flutter-wwise.git
      ref: main
```

## Planned API

The surface is deliberately small. Anything not listed here is out of scope
until the core is solid.

```dart
import 'package:flutter_wwise/flutter_wwise.dart';

// Initialise the engine against your SDK's Init.bnk
await Wwise.init(
  initBankPath: 'assets/wwise/Init.bnk',
  sampleRate: 48000,
);

// Load a SoundBank produced by the Wwise authoring tool
await Wwise.loadBank('Main.bnk');

// Game objects identify "who is making this sound"
final player = await Wwise.registerGameObject('player');

// Fire an Event defined in Wwise
await Wwise.postEvent('Play_IceDrop', player);

// Drive a Game Parameter in real time — e.g. how hard the shaker is shaken
await Wwise.setRtpc('ShakeIntensity', 0.82, player);

// Render the audio graph; call once per audio callback / frame budget
await Wwise.renderAudio();

await Wwise.unregisterGameObject(player);
await Wwise.term();
```

## Building

Set `WWISE_SDK_ROOT` to your Wwise SDK directory (the one containing
`include/`, `x64_vc170/`, `Linux_x64/`, `Android_arm64-v8a/`, …):

```bash
# Windows (PowerShell)
$env:WWISE_SDK_ROOT = "C:\Program Files (x86)\Audiokinetic\Wwise 2024.1.14.9084\SDK"

# Linux / macOS
export WWISE_SDK_ROOT=/opt/wwise/2024.1.14.9084/SDK
```

Then build the example app:

```bash
cd example
flutter run -d windows
```

The CMake glue in `src/cmake/` resolves the correct per-platform library from
`WWISE_SDK_ROOT` and links it into the plugin.

## Scope and non-goals

**In scope**

- Initialise / terminate the sound engine
- Load and unload SoundBanks
- Register and unregister game objects
- Post Events
- Set Game Parameters (RTPC) and switches/states
- Drive `RenderAudio` from the host
- Sensible Dart-typed errors, not raw integer codes

**Out of scope (for now)**

- The Wwise authoring tool (use `WwiseConsole`; see [docs](docs/))
- Spatial audio beyond what the engine does by default
- Bundling or downloading Wwise SDK content

## Wwise licensing

This project is **independent and unofficial** — not affiliated with, endorsed
by, or sponsored by Audiokinetic Inc.

Wwise is commercial software. You need your own licence, and you are
responsible for complying with its terms — **including the conditions under
which Wwise runtime libraries may be redistributed inside a shipped
application.** Audiokinetic's own licensing pages are the authority here; this
README is not legal advice.

## Related

- [`pywwise`](https://pypi.org/project/pywwise/) — WAAPI wrapper for Python
  (drives the **authoring** tool, not the runtime; complementary to this package)
- [`ww2ogg`](https://github.com/hcs64/ww2ogg) — converts Wwise RIFF/RIFX Vorbis
  to standard Ogg Vorbis, useful for extracting audio from SoundBanks
- Wwise SDK documentation, shipped with your installation under `SDK/Help/`

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Issues and pull requests are welcome —
especially platform build reports, since CI cannot cover every Wwise version.

## License

MIT — see [LICENSE](LICENSE). The Wwise SDK itself is **not** covered by this
license and is **not** distributed here.

---

*README auf Deutsch? Nein. 中文版见 [README.zh-CN.md](README.zh-CN.md)。*
