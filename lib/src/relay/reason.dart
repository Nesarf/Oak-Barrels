/// Failure reason codes, per `docs/RELAY_PROTOCOL.md` §6.
///
/// A reason code is the *only* thing a relay is allowed to say about a failure.
/// It must never be accompanied by text naming a vendor, a version, or a
/// filesystem path — that constraint is what keeps diagnostics from turning
/// into a fingerprint of the user's machine.
///
/// The [summary] attached to each code is therefore deliberately generic: it
/// describes the *class* of problem, never the specific thing that was found.
library;

/// Reason codes carried by `ERROR` frames and by the `STATUS` frame.
enum ReasonCode {
  ok(0, 'ok'),
  noEngine(1, 'no candidate installation found'),
  noCommonRevision(2, 'protocol revisions do not overlap'),
  incompatibleEngine(3, 'no known compatibility class fits'),
  bindFailed(4, 'dynamic symbol resolution failed'),
  unsupportedOperation(5, 'capability not present for this engine'),
  invalidHandle(6, 'unknown or closed target'),
  badArgument(7, 'malformed or out-of-range argument'),
  busy(8, 'another session holds the relay'),
  internal(9, 'internal error'),
  diagDisabled(10, 'diagnostics requested without opt-in');

  const ReasonCode(this.code, this.summary);

  /// The numeric code written on the wire.
  final int code;

  /// A generic, non-identifying description. Safe to log and to show to users.
  final String summary;

  /// Whether this code represents success.
  bool get isOk => this == ReasonCode.ok;

  /// Returns the code for [code], or null when the value is not defined.
  ///
  /// An unknown code is returned as null rather than thrown so that a host
  /// built against an older revision degrades rather than crashes when a newer
  /// relay introduces a code it has never seen.
  static ReasonCode? tryFromCode(int code) {
    for (final reason in ReasonCode.values) {
      if (reason.code == code) return reason;
    }
    return null;
  }
}
