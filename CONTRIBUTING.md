# Contributing to flutter_wwise

Thanks for considering a contribution. This project exists as a **neutral
relay** — it belongs to neither side of the pipe. Keeping it that way is the
main thing contributors need to help with.

## Before you start

Nothing needs to be installed to work on the relay. It has **no third-party
build dependencies**, by design — it links no vendor SDK at build time.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

dart pub get
dart test
```

If a change you are making would require reading a vendor SDK path at configure
time, stop and open an issue first. That is a design regression, not a feature.

## The two rules that keep this project neutral

### Rule 1 — Nothing identifying gets committed

**No version numbers, no absolute paths, no machine details, no build
fingerprints.** Not in source, not in docs, not in comments, not in fixtures,
not in issue descriptions pasted into the repository.

Concretely, do not commit:

| Never commit | Why |
| --- | --- |
| Exact version strings of any tool, SDK, runtime or library | Turns the repository into a fingerprint of one machine |
| Absolute paths (`C:\...`, `/home/...`, `/opt/...`) | Leaks user names, directory layouts, organisational context |
| Recorded environment dumps, `--version` output pasted as evidence | Same, in bulk |
| Vendor product names in code paths, defaults, or protocol constants | Makes the relay a de-facto integration for one product |
| Machine or user identifiers of any kind | Should never be collected at all, let alone stored |

Where documentation needs to illustrate a command, use a **placeholder**:

```bash
# Good
export RT_SDK_DIR=/path/to/your/installation

# Bad — this is somebody's actual disk layout
export RT_SDK_DIR=/opt/vendor/2024.1.14.9084/SDK
```

Vendor names are permitted in exactly two places: the LICENSE file (where a
non-affiliation statement is legally meaningful) and prose that explicitly
discusses a third-party project by name. They must never appear in code,
constants, defaults, or the wire protocol.

CI enforces this. See the `hygiene` job in `.github/workflows/ci.yml`.

### Rule 2 — Nothing is known at build time

A compile-time constant that encodes a version, a path, or a vendor is a bug,
however convenient it is. Compatibility is expressed as **classes** (see
`docs/RELAY_PROTOCOL.md` §4.3 and §10), and classes are decided at run time.

When signals are ambiguous, map to a **narrower** class, never a broader one.
Under-claiming degrades gracefully. Over-claiming crashes on the user's machine.

## What we need most

| Area | Why it matters |
| --- | --- |
| **Transport implementations** | Unix socket, named pipe, stdio. Intrinsically platform-shaped; patches welcome. |
| **Discovery strategies** | Finding candidate installations from neutral signals is the hardest correctness problem here. Conservative heuristics with honest failure modes are valuable. |
| **Protocol review** | The protocol is the expensive thing to change later. Argument with it now is cheaper than argument after implementation. |
| **Redaction adversarial review** | Try to find a path by which a version or path still leaks. Then tell us. |
| **Cross-platform build reports** | Which platforms and toolchains actually work, stated without naming versions you would rather not disclose. |

## Ground rules

1. **Keep the wire protocol small.** Additions need an issue and a rationale.
   A narrow protocol both sides can implement beats a rich one that half-works.
2. **Failures must be typed.** Reason codes per protocol §6, never free-form
   strings that happen to contain identifying text.
3. **Diagnostics stay opt-in and redacted.** Anything that increases what the
   relay says by default needs a very good argument.
4. **No persistence of discovered facts.** If you find yourself wanting to
   cache something keyed by path or version, that is the host's job, not ours.
5. **One concern per pull request.**

## Commit messages

Conventional Commits style; scope optional but appreciated.

```
feat(transport): add unix domain socket transport
feat(discovery): conservative class mapping for ambiguous signals
fix(protocol): reject zero-length frames before reading payload
docs(protocol): clarify that reason codes must not carry paths
build(cmake): remove configure-time vendor path lookup
```

## Pull request checklist

- [ ] `cmake --build build` succeeds
- [ ] `dart analyze` is clean
- [ ] `dart test` passes
- [ ] No version strings, absolute paths, or vendor names introduced outside
      the two permitted places (checked by the CI hygiene job)
- [ ] No new build-time dependency on any vendor SDK
- [ ] `CHANGELOG.md` updated under `[Unreleased]`
- [ ] Protocol changes are reflected in `docs/RELAY_PROTOCOL.md`, which is
      normative — if the doc and the code disagree, the doc wins and the code
      is wrong

## Reporting a build failure

Please include the **full** build log and the operating system. You are
**not** expected to disclose exact tool, SDK or runtime versions, and you
should not paste absolute paths. If a maintainer needs more detail to
reproduce, they will ask — and you can answer in terms of compatibility
classes rather than builds.

## License

By contributing, you agree that your contributions are licensed under the
[MIT License](LICENSE).
