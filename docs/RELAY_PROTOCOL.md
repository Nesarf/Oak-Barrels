# Relay Protocol

> The contract between a **host** (anything that can open a local pipe) and the
> **relay station**. This document is normative. Where it disagrees with any
> code, this document wins and the code is wrong.

---

## 0. Design axioms

These five rules generate most of the decisions below.

**A1 -- The relay belongs to no vendor.**
It is not an integration for a named product. Product names may appear as
*examples* in prose, never as protocol requirements, defaults, or branches.

**A2 -- Nothing is known at build time.**
Every engine, every version, every capability is discovered at run time. A
compile-time constant that encodes a version is a bug.

**A3 -- Compatibility is a class, not a version.**
The relay never needs to know that a user has build *X*. It needs to know
which *family of calling conventions* the engine speaks. Exact versions are
transient facts; compatibility classes are stable ones.

**A4 -- Discovered facts stay in memory.**
Paths, versions, and identifiers learned during discovery are not persisted,
not logged by default, and not transmitted unless a capability explicitly
requires it and the host has opted in.

**A5 -- Every engine is assumed unlicensed.**
The relay assumes that no engine on this machine carries a licence granted to
this project. Nothing in discovery, probing or binding may depend on a licensed
capability, and a capability that is licence-gated is reported as **absent**
rather than assumed present.

Two consequences follow, and both point the same way. The relay never
redistributes content that a licence would have permitted it to ship, and it
never claims a behaviour it cannot verify. The second is the same conservatism
that A3 and section 10 already demand of class mapping: an unverifiable
capability is an absent one.

---

## 1. Lifecycle

```
        +-----------+
        |  discover |  find candidate installations from neutral signals
        +-----+-----+
              v
        +-----------+
        |   probe   |  determine capabilities, without assuming versions
        +-----+-----+
              v
        +-----------+
        | negotiate |  agree on a protocol revision + feature set
        +-----+-----+
              v
        +-----------+
        |   bind    |  resolve symbols dynamically
        +-----+-----+
              v
        +-----------+
        |   relay   |  forward calls, return results
        +-----------+
```

Failure at any stage is reportable as a **reason code** (see section 6) without
revealing what was found.

---

## 2. Transport

The relay supports three transports. A conforming implementation must provide
at least one; a host picks whichever its platform makes natural.

| Transport | Platform fit | Notes |
| --- | --- | --- |
| **Unix domain socket** | Linux, macOS, Android | Filesystem path or abstract namespace |
| **Named pipe** | Windows | No filesystem artifact required |
| **stdio** | Everywhere | Host spawns the relay and speaks over its standard streams |

**Transport rules**

- The relay never listens on a TCP port by default. Network-exposed audio
  control is a decision the host must make explicitly, not a default.
- When a socket or pipe has a name, that name must be chosen by the host. The
  relay does not invent predictable names.
- A single relay instance serves exactly one host connection. Additional
  connections are refused rather than queued.

---

## 3. Framing

Messages are length-prefixed frames. All integers are **little-endian**.

```
+--------+--------+-------------------+
| u32 len| u8 type|     payload       |
+--------+--------+-------------------+
  4 bytes  1 byte   len-1 bytes
```

- `len` counts the type byte **plus** payload. A frame with `len < 1` is a
  protocol error and the connection is closed.
- Maximum frame size is 1 MiB for control messages. Bulk audio does not travel
  over this protocol (see section 7).
- Payload encoding is declared during negotiation (section 4.2). The default is
  UTF-8 JSON; implementations may negotiate a denser encoding.

### 3.1 Message types

| Value | Name | Direction | Purpose |
| --- | --- | --- | --- |
| `0x01` | `HELLO` | host -> relay | Open a session, declare protocol revisions supported |
| `0x02` | `HELLO_ACK` | relay -> host | Accept a revision, declare relay revision |
| `0x10` | `CAPS_REQUEST` | host -> relay | Ask what the relay can offer |
| `0x11` | `CAPS_REPLY` | relay -> host | Capability classes available |
| `0x20` | `OPEN` | host -> relay | Open a target (an emitter/voice/channel) |
| `0x21` | `OPEN_ACK` | relay -> host | Target handle |
| `0x30` | `POST` | host -> relay | Fire a named action at a target |
| `0x31` | `SET` | host -> relay | Set a named continuous parameter |
| `0x40` | `CLOSE` | host -> relay | Close a target |
| `0x50` | `STATUS` | relay -> host | Asynchronous state / reason codes |
| `0x60` | `DIAG_REQUEST` | host -> relay | Ask for diagnostics (opt-in, see section 8) |
| `0x61` | `DIAG_REPLY` | relay -> host | Redacted diagnostics |
| `0x70` | `BYE` | both | Terminate cleanly |
| `0x7F` | `ERROR` | both | Protocol or operation failure, with a reason code |

---

## 4. Negotiation

### 4.1 Protocol revision

`HELLO` carries a set of protocol revisions the host supports. `HELLO_ACK`
carries the single revision the relay selects. Selection is the **highest
mutually supported** revision. No overlap means the relay replies `ERROR` with
`NO_COMMON_REVISION` and closes.

### 4.2 Feature flags

Beyond the revision, both sides declare optional features:

```
HELLO     { "revisions": [...], "features": [...] }
HELLO_ACK { "revision": N,      "features": [...] }
```

Unknown feature strings are ignored, never fatal. This is what lets one side
gain a feature without waiting for the other.

### 4.3 Capability classes

After `HELLO_ACK`, the host may send `CAPS_REQUEST`. The reply describes the
backing engine in terms of **compatibility classes**, not versions:

```json
{
  "classes": ["engine.action.named", "engine.param.continuous", "engine.bus.hierarchical"],
  "limits":  { "max_targets": 64, "max_param_rate_hz": 60 },
  "bulk":    false
}
```

- A **class** names a *family of behaviours*, not a build. `engine.action.named`
  means "there is a way to trigger something by name". It does not mean "build
  X.Y.Z".
- Class names are intentionally generic. A relay must be able to report its
  classes even when the backing engine is one this project has never seen.
- If no class covers what the host needs, the host degrades gracefully rather
  than probing for more detail.

---

## 5. Operations

### `OPEN`

```json
{ "kind": "emitter", "name": "ui" }
```

`kind` is generic: `emitter`, `bus`, `global`. The relay maps it to whatever
the backing engine calls that concept. `name` is chosen by the host and is not
required to be globally unique.

Reply: `{ "handle": 42 }`. Handles are opaque, non-zero, and valid until
`CLOSE` or session end.

### `POST`

```json
{ "handle": 42, "action": "ice_drop", "args": { } }
```

`action` is a *name*, resolved by the backing engine. The relay does not carry
a catalogue of valid names, because that would be a version-specific fact.

### `SET`

```json
{ "handle": 42, "param": "intensity", "value": 0.82 }
```

`value` is a double in `[0, 1]` unless a negotiated feature says otherwise.
This is the hook for real-time control -- sensor input, user gesture, anything
continuous.

### `CLOSE`

```json
{ "handle": 42 }
```

Idempotent. Closing an unknown handle is not an error.

---

## 6. Reason codes

Failures are reported as a code plus a short, **non-identifying** phrase.

| Code | Name | Meaning |
| --- | --- | --- |
| `0` | `OK` | -- |
| `1` | `NO_ENGINE` | No candidate installation found |
| `2` | `NO_COMMON_REVISION` | Protocol revisions do not overlap |
| `3` | `INCOMPATIBLE_ENGINE` | An installation exists but no known class fits |
| `4` | `BIND_FAILED` | Dynamic resolution of a required symbol failed |
| `5` | `UNSUPPORTED_OPERATION` | Capability not present for this engine |
| `6` | `INVALID_HANDLE` | Unknown or closed target |
| `7` | `BAD_ARGUMENT` | Malformed or out-of-range argument |
| `8` | `BUSY` | Another session holds the relay |
| `9` | `INTERNAL` | Should not happen; a bug |
| `10` | `DIAG_DISABLED` | Diagnostics requested without opt-in |

A reason code must never be accompanied by a message that names a vendor, a
version, or a filesystem path.

---

## 7. Audio path

**Audio does not cross this pipe by default.**

The relay's job is *control*. Where the audio goes is the backing engine's
business -- it drives the system audio device directly, as it would for any
other host.

A future revision may negotiate a **bulk channel** for hosts that need to
process the audio themselves. If it does, that channel must be:

- a separate transport (shared memory, or a second socket), never the control
  pipe, so a slow audio consumer cannot stall control messages;
- declared as a capability class (`engine.bulk.pcm`) so hosts can detect its
  absence rather than discovering it by failure;
- silent about formats it cannot honour.

---

## 8. Diagnostics and redaction

Diagnostics are **off unless the host opts in** via a `DIAG_REQUEST` carrying
`{ "enable": true }`.

When enabled, output is **redacted by default**:

| Raw fact | Redacted form |
| --- | --- |
| Filesystem path | Opaque id, e.g. `inst-a3f1`, stable only within the session |
| Exact version string | Compatibility class, e.g. `class.b` |
| Vendor / product name | Omitted entirely unless the host opts into `identify` |
| Symbol names resolved | Count only |
| Machine or user identifiers | Never collected, therefore never reported |

Two independent switches govern detail:

- `diagnose` -- allow diagnostics at all.
- `identify` -- allow vendor and product names to appear. Off by default.

A host that never sets `identify` can run for years without the relay ever
telling it what it is talking to, and that is the intended posture.

---

## 9. Persistence

The relay **must not** write discovered facts to disk. Specifically, it must
not create:

- caches keyed by version or path,
- logs containing paths or version strings,
- lock files whose names encode installation identity,
- any artifact outside a directory the host explicitly nominated.

State that must survive a restart -- such as which installation the host
prefers -- is the **host's** to remember, not the relay's. This keeps the relay
stateless with respect to identification, which is what makes axiom A4
enforceable rather than aspirational.

---

## 10. Version compatibility strategy

Because compatibility is expressed as classes (A3), the relay maintains an
internal mapping:

```
observed engine signals  ->  compatibility class  ->  calling convention
```

The middle column is the only thing that crosses the pipe. The mapping itself
lives in the relay and is **allowed to be wrong in detail, as long as it is
conservative**: when signals are ambiguous, the relay must report a *narrower*
class, never a broader one. Under-claiming degrades gracefully; over-claiming
produces crashes at the user's expense.

Unknown signals map to no class at all. The relay then reports
`INCOMPATIBLE_ENGINE` rather than guessing.

---

## 11. Conformance

A conforming relay:

1. Selects a transport from section 2 and refuses a second concurrent session.
2. Implements section 3 framing exactly, including the 1 MiB control limit.
3. Answers `HELLO` with the highest mutually supported revision.
4. Ignores unknown feature strings.
5. Reports capabilities as classes, never versions.
6. Never emits a vendor name, version string, or path unless `identify` is on.
7. Writes nothing to disk outside a host-nominated directory.
8. Maps unknown signals to no class, not a guessed one.

A conforming host:

1. Sends `HELLO` before anything else.
2. Treats unknown capability classes as absent rather than fatal.
3. Never sends a path expecting the relay to persist it.
4. Opts into diagnostics explicitly if it wants them.

---

## Appendix A -- Why length-prefixed frames rather than line-delimited JSON

Line-delimited JSON is easier to debug by eye. Length-prefixed framing was
chosen anyway because:

- payloads may legitimately contain newlines (names chosen by the host),
- a single malformed frame must be detectable without scanning for a delimiter,
- a future binary encoding can be negotiated without changing the transport.

A `--debug-text` mode may wrap frames in a human-readable transcript for
authoring purposes. That mode is a development aid and is not part of the
protocol.
