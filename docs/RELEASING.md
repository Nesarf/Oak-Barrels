# Releasing

> Everything a release needs, in the order it has to happen. The version number
> lives in three files and nowhere else, and the packages are built by a
> workflow against the tag -- never by hand.

## 1. Decide the number

Semantic versioning, as `CHANGELOG.md` states it. A release that adds capability
without removing any is a minor bump: 0.2.0 after 0.1.0. There is no build
metadata in the number, and no version of any engine in it either.

## 2. Set the number in three places

| File | What to change |
| --- | --- |
| `pubspec.yaml` | `version:` |
| `CMakeLists.txt` | `VERSION` in `project(...)` |
| `CHANGELOG.md` | the `## [Unreleased]` heading becomes `## [x.y.z] - YYYY-MM-DD`, and an empty `## [Unreleased]` stays above it |

Only the CMake copy reaches the binary: the station prints it from `--version`
and knows nothing else's. A test or a document quoting a version is quoting one
of these three, so if the three disagree the release is wrong before it starts.

## 3. Check the tree

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
dart pub get
dart test
```

The CI matrix runs the same commands on Linux, macOS and Windows. Tag only with
that run green, because the tag is what the packages are built from.

## 4. Commit, tag, push

```bash
git add pubspec.yaml CMakeLists.txt CHANGELOG.md
git commit -m "chore(release): x.y.z"
git tag -a vx.y.z -m "oak_barrels x.y.z"
git push origin main
git push origin vx.y.z
```

The tag is annotated: it carries who cut the release and when, which a
lightweight tag does not.

## 5. Create the release, then build the packages onto it

A tag and a release are two different things. `gh release upload` attaches to
the release, and a tag on its own has none. Create the release first, with the
changelog section as its notes:

```bash
gh release create vx.y.z --repo Nesarf/Oak-Barrels \
  --title "oak_barrels x.y.z" --notes-file release-notes.md
```

The `package` job only runs on `workflow_dispatch`, so an ordinary push never
publishes anything. Start it by hand, naming the tag it should attach to:

```bash
gh workflow run CI --repo Nesarf/Oak-Barrels --ref main -f release_tag=vx.y.z
```

Three jobs then build the archive for their own platform with CPack and attach
it to that tag. Each one prints the release's asset list before and after
uploading, so a replacement is a line in the log rather than a surprise.

Verify:

```bash
gh release view vx.y.z --repo Nesarf/Oak-Barrels --json assets --jq '.assets[].name'
```

There should be one archive per platform, named
`oak-barrels-<version>-<system>-<arch>` with `.zip` on Windows and `.tar.gz`
elsewhere. Unpack one and run `bin/oak-barrels --version`; it must answer the
number from step 2.

## 6. What not to do

- Do not move a tag once it is pushed. Archive names embed the version, so a
  moved tag turns a published download into one nobody can attribute.
- Do not hand-build an archive and upload it. The job builds from the dispatched
  ref and leaves that fact in the log; a hand-built archive leaves nothing.
- Do not fold unrelated edits into the release commit. The version bump is a
  three-line change, and keeping it that way is what makes it readable a year
  later.
- Do not assume a tag is a release. The first run through this document pushed
  the tag and started the packages, and all three jobs reported "release not
  found", because there was nothing to attach them to.
