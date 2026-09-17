# Contributing to flutter_wwise

Thanks for considering a contribution. This project exists because Audiokinetic
does not ship a Flutter integration — so community effort is the whole point.

## Before you start

**You need your own Wwise installation.** This repository deliberately contains
no Wwise SDK files. Install Wwise via the Audiokinetic Launcher, then point
`WWISE_SDK_ROOT` at its `SDK` directory:

```powershell
$env:WWISE_SDK_ROOT = "C:\Program Files (x86)\Audiokinetic\Wwise <VERSION>\SDK"
```

```bash
export WWISE_SDK_ROOT=/opt/wwise/<VERSION>/SDK
```

Never commit anything from the SDK. `.gitignore` already blocks the common
paths, but please double-check before opening a pull request.

## Development setup

```bash
flutter --version          # >= 3.22
dart --version             # >= 3.4

flutter pub get
dart run build_runner build --delete-conflicting-outputs   # ffigen bindings (once wired)
```

## What we need most

| Area | Why it matters |
| --- | --- |
| **Platform build reports** | CI cannot cover every Wwise version × platform combination. If you got it building on your machine, say so — with your Wwise version, OS, and compiler. |
| **SDK version differences** | Wwise changes its headers across releases. A shim that compiles on 2024.1 but not 2023.1 is a bug worth reporting. |
| **Audio backend integration** | Getting the engine to actually output sound on each platform is the hard part. Notes, patches and failure logs are all welcome. |
| **Documentation** | Especially the "I wish I'd known this earlier" kind. |

## Ground rules

1. **Keep the public Dart API small.** A narrow surface that works beats a broad
   one that half-works. Propose additions in an issue first.
2. **No SDK files, ever.** Not headers, not `.lib`, not `.dll`, not `.a`.
   Nothing from an Audiokinetic installation belongs in a commit.
3. **Errors must be typed.** Do not leak raw Wwise integer return codes to Dart
   callers; map them to exceptions with meaning.
4. **One concern per pull request.** Platform build fixes and API changes should
   not travel together.

## Commit messages

Conventional Commits style, scope optional but appreciated:

```
feat(shim): add hc_wwise_set_switch
fix(windows): resolve SDK path with spaces correctly
docs(readme): correct Android ABI list
build(cmake): link AkSoundEngine statically on Linux
```

## Pull request checklist

- [ ] `flutter analyze` is clean
- [ ] `flutter test` passes
- [ ] No Wwise SDK content is staged (`git status` reviewed)
- [ ] `CHANGELOG.md` updated under `[Unreleased]`
- [ ] If the change is platform-specific, the platform and Wwise version are stated

## Reporting a build failure

Please include:

- OS and version
- Wwise version (e.g. `2024.1.14.9084`)
- Flutter and Dart versions
- Compiler / NDK version
- The **full** build log, not the last line
- Whether `WWISE_SDK_ROOT` was set, and to what

## License

By contributing, you agree that your contributions are licensed under the
[MIT License](LICENSE).
