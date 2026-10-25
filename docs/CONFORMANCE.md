# Conformance map

> `docs/RELAY_PROTOCOL.md` section 11 states what a conforming relay and a
> conforming host must do. This document says where each of those clauses is
> checked, so a reader can tell a claim from a habit.

Names below are test names. Bare `snake_case` names live in `tests/`, the
native suite. Quoted names live in `test/`, the Dart suite. Names beginning
`end to end:` are in `test/end_to_end_test.dart`, which drives a real station
process over a real transport.

## A conforming relay

| Clause | Where it is checked |
| --- | --- |
| 1. Selects a transport from section 2, and refuses a second concurrent session | `named_pipe_carries_bytes_both_ways`, `unix_socket_carries_bytes_both_ways`, `unix_socket_refuses_to_clobber_a_path_in_use`; `end to end: refuses to serve two transports at once`, `end to end: refuses a second connection on the same endpoint` |
| 2. Implements section 3 framing exactly, including the 1 MiB control limit | `frame_encodes_the_length_prefix_little_endian`, `frame_encodes_a_payloadless_message_with_length_one`, `frame_encodes_a_multi_byte_length`, `frame_decodes_a_frame_split_across_feeds`, `frame_rejects_a_length_below_one`, `frame_rejects_a_frame_over_the_control_budget`, `frame_accepts_a_length_exactly_at_the_budget`, `frame_rejects_an_unknown_message_type`, `frame_failure_is_sticky_and_ignores_later_input`; Dart: `length field counts the type byte, not just the payload`, `length prefix is little-endian`, `reassembles a frame split across arbitrary chunk boundaries`, `rejects a zero-length frame`, `rejects a frame larger than the control budget`, `rejects an unknown message type` |
| 3. Answers `HELLO` with the highest mutually supported revision | `session_negotiates_the_highest_common_revision`, `session_reports_no_common_revision_and_closes`; `end to end: negotiates revision 1 over a real pipe` |
| 4. Ignores unknown feature strings | `session_ignores_unknown_features` |
| 5. Reports capabilities as classes, never versions | `session_reports_capabilities_as_classes_not_versions`, `session_never_names_a_vendor_or_version_in_a_reason`; Dart: `parses classes, limits and the bulk flag`; `end to end: reports no capability classes, because it has no engine` |
| 6. Never emits a vendor name, version string or path unless `identify` is on | `session_never_names_a_vendor_or_version_in_a_reason`, `scanner_ids_are_opaque_and_do_not_leak_the_path`, `module_format_names_are_short_and_neutral`; Dart: `summaries carry no identifying text`; `end to end: opting in yields a report that identifies nothing`, `end to end: says nothing about an engine when asked for its own version` |
| 7. Writes nothing to disk outside a host-nominated directory | `end to end: never touches the filesystem when no root was nominated`, `end to end: looks only where the host told it to, and still claims nothing` |
| 8. Maps unknown signals to no class, not a guessed one | `prober_reports_no_binding_when_there_are_no_profiles`, `prober_reports_no_binding_when_a_symbol_is_absent`, `module_format_rejects_what_is_not_a_module`; `end to end: reports no capability classes, because it has no engine` |

## A conforming host

| Clause | Where it is checked |
| --- | --- |
| 1. Sends `HELLO` before anything else | Dart: `sends HELLO with our revisions and records the agreed one`. The relay side of the same rule is `session_rejects_a_message_before_hello`. |
| 2. Treats unknown capability classes as absent rather than fatal | Dart: `treats a missing classes field as empty rather than fatal`, `unknown reason codes map to null instead of throwing` |
| 3. Never sends a path expecting the relay to persist it | Structural: `Relay.open` carries a `kind` and a name, and no API accepts a path to store. There is nothing to test because there is nothing to call. |
| 4. Opts into diagnostics explicitly | `end to end: diagnostics are refused until the host opts in`; Dart: `Relay.diagnostics` sends no `enable` field unless the caller asks |

## Why keep this map

A clause with no test name beside it is a clause nobody is checking. Section 3
of the protocol is the expensive part to get wrong, which is why it carries the
most names. When section 11 gains a clause, it gains a row here and at least
one name in the same change.

Row 3 of the host table is the one that shows what this map is for: it is not a
test, it is a claim about the shape of the API, and writing it down is how we
notice if that shape ever changes.
