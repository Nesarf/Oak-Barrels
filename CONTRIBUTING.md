# Contributing to oak_barrels

Thanks for considering a contribution. This project exists as a **neutral
relay** -- it belongs to neither side of the pipe. Keeping it that way is the
main thing contributors need to help with.

## Before you start

Nothing needs to be installed to work on the relay. It has **no third-party
build dependencies**, by design -- it links no vendor SDK at build time.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

dart pub get
dart test
```

If a change you are making would require reading a vendor SDK path at configure
time, stop and open an issue first. That is a design regression, not a feature.

## The two rules that keep this project neutral

### Rule 1 -- Nothing identifying gets committed

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

# Bad -- this is somebody's actual disk layout
export RT_SDK_DIR=/opt/vendor/2024.1.14.9084/SDK
```

**We do not define ourselves by a target.**

This project declares **no applicable target**. It is not the plugin *for*
anything and not the binding *to* anything, and it does not write down a
pairing it was built for. A relay that announced "this is for A and B" would
become an integration for A and B the moment those names were written, and
would be quietly wrong the moment either of them moved. So it does not write
them down.

Vendor names are permitted in exactly one place: the LICENSE file, where a
non-affiliation statement is legally meaningful. Everywhere else a name may
appear only when the text is **discussing** a third-party project, never when
the text is **defining** this one.

| Use | Allowed? |
| --- | --- |
| "This is not the A plugin for B" | No. That defines us by a target we do not have. |
| "Compared with project C, this keeps the protocol narrower" | Yes. That discusses somebody else's work. |
| A default, constant, path, class name or protocol string containing a product name | No, at any time. |

The relay is defined by what it does -- carry control between a host and an
engine -- not by who it is for. Who uses it is the user's business, and per
axiom A4 it is not something the relay needs to know.

CI enforces this. See the `hygiene` job in `.github/workflows/ci.yml`.

### Rule 2 -- Nothing is known at build time

A compile-time constant that encodes a version, a path, or a vendor is a bug,
however convenient it is. Compatibility is expressed as **classes** (see
`docs/RELAY_PROTOCOL.md` section 4.3 and section 10), and classes are decided at run time.

When signals are ambiguous, map to a **narrower** class, never a broader one.
Under-claiming degrades gracefully. Over-claiming crashes on the user's machine.

## Text and encoding

**Tracked text is ASCII only.** Code, comments, documentation, commit messages,
fixtures -- if it is in the repository, it is ASCII.

The reason is not aesthetic. A compiler that decodes source using the machine's
code page builds the same file differently in different regions, and that is not
a warning to be silenced: it is the same source producing different binaries. It
turned up during the C++ port as compiler warnings on a non-Western code page,
and the fix was to remove the non-ASCII characters rather than to declare an
encoding and hope every toolchain honours it.

Every substitution is one character wide, so diagrams and aligned tables survive
them exactly:

| Instead of | Write |
| --- | --- |
| an em dash | `--` |
| a section sign | the word `section`, as in `section 4.3` |
| a right arrow | `->` |
| an ellipsis | `...` |
| box-drawing characters | `+`, `-`, `|` |
| a check or status glyph | `[x]`, `[ ]`, `[~]` |

CI enforces this. See the `hygiene` job in `.github/workflows/ci.yml`.

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
2. **Failures must be typed.** Reason codes per protocol section 6, never free-form
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
      the permitted place (checked by the CI hygiene job)
- [ ] Tracked text is ASCII
- [ ] No new build-time dependency on any vendor SDK
- [ ] `CHANGELOG.md` updated under `[Unreleased]`
- [ ] Protocol changes are reflected in `docs/RELAY_PROTOCOL.md`, which is
      normative -- if the doc and the code disagree, the doc wins and the code
      is wrong

## Reporting a build failure

Please include the **full** build log and the operating system. You are
**not** expected to disclose exact tool, SDK or runtime versions, and you
should not paste absolute paths. If a maintainer needs more detail to
reproduce, they will ask -- and you can answer in terms of compatibility
classes rather than builds.

## License

By contributing, you agree that your contributions are licensed under the
[MIT License](LICENSE).
