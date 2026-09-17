/// A reference host.
///
/// Run it against a built station:
///
///     dart run example/reference_host.dart ./build/oak-barrels
///
/// It walks the lifecycle the protocol describes -- negotiate, ask what the
/// station can offer, try to use it, degrade when it will not, shut down -- and
/// prints what it learned.
///
/// Two things it deliberately never learns: what the station is, and which
/// build of it. The host does not ask, because the protocol is arranged so that
/// it does not need to. Everything here is expressed in compatibility classes
/// and reason codes, so the same code works against any station that speaks
/// this revision -- including one backing an engine this host has never heard
/// of.
library;

import 'dart:io';

import 'package:oak_barrels/oak_barrels.dart';

/// The transcript separator, so the output reads as one conversation.
const String _rule = '----------------------------------------';

Future<void> main(List<String> arguments) async {
  if (arguments.length != 1) {
    stderr.writeln('usage: reference_host <path-to-oak-barrels>');
    exitCode = 2;
    return;
  }

  stdout.writeln('spawning a station');
  final relay = await Relay.spawn(arguments.single);

  try {
    await _run(relay);
  } on RelayException catch (error) {
    // A reason code, and a phrase drawn from a fixed table. Neither names a
    // vendor, a version or a path, so this line is safe to log, ship, or paste
    // into a bug report.
    stderr.writeln('the station refused: '
        '${error.reason?.name ?? 'unknown code'} '
        '(${error.detail ?? 'no detail'})');
    exitCode = 1;
  } finally {
    await relay.dispose();
  }

  stdout.writeln(_rule);
  stdout.writeln('done');
}

Future<void> _run(Relay relay) async {
  // 1. Negotiate. The station picks the highest revision we both speak, and
  //    says NO_COMMON_REVISION rather than guessing if there is no overlap.
  final revision = await relay.negotiate();
  stdout.writeln('negotiated protocol revision $revision');
  stdout.writeln(_rule);

  // 2. Ask what it can do. The answer is classes of behaviour, never versions:
  //    "there is a way to trigger something by name" rather than "build X".
  final caps = await relay.capabilities();
  stdout.writeln('compatibility classes offered:');
  if (caps.classes.isEmpty) {
    stdout.writeln('  (none)');
  } else {
    for (final name in caps.classes) {
      stdout.writeln('  $name');
    }
  }
  stdout.writeln('bulk audio channel: '
      '${caps.bulk ? 'offered on a separate transport' : 'not offered'}');
  stdout.writeln(_rule);

  // 3. Degrade, do not probe. If a class we wanted is absent, we drop the
  //    feature that needed it. Asking the station for more detail to work
  //    around the gap is exactly the version-sniffing this protocol exists to
  //    avoid.
  if (!caps.supports('engine.action.named')) {
    stdout.writeln('no action-capable class: one-shot actions will be skipped');
  }
  if (!caps.supports('engine.param.continuous')) {
    stdout.writeln('no continuous-parameter class: real-time control will be '
        'skipped');
  }
  stdout.writeln(_rule);

  // 4. Try to open a target. Kinds are generic -- the station maps them to
  //    whatever the engine behind it calls the concept.
  final handle = await _openOrExplain(relay);
  if (handle == null) {
    stdout.writeln('carrying on without a target');
  } else {
    await relay.post(handle, 'ice_drop', args: <String, Object?>{'gain': 0.8});
    stdout.writeln('posted an action to target $handle');

    await relay.set(handle, 'intensity', 0.82);
    stdout.writeln('set a continuous parameter on target $handle');

    await relay.close(handle);
    stdout.writeln('closed target $handle');
  }
  stdout.writeln(_rule);

  // 5. Diagnostics are opt-in, and identification is a second switch that
  //    stays off unless asked for. Querying without opting in is refused,
  //    which is the point: the station does not volunteer what it knows.
  await _showDiagnostics(relay);
  stdout.writeln(_rule);

  // 6. Say goodbye. BYE is acceptable at any point in a session.
  await relay.shutdown();
  stdout.writeln('station shut down');
}

/// Opens an emitter, or returns null after explaining why it could not.
Future<int?> _openOrExplain(Relay relay) async {
  try {
    final handle = await relay.open('emitter', name: 'reference-host');
    stdout.writeln('opened an emitter as target $handle');
    return handle;
  } on RelayException catch (error) {
    // NO_ENGINE is a normal answer, not a crash. A station that has found
    // nothing to talk to says so, and a host carries on with whatever it can
    // still do. Over-claiming here would be the station's fault; treating this
    // as exceptional would be ours.
    stdout.writeln('could not open an emitter: '
        '${error.reason?.name ?? 'unknown code'}');
    return null;
  }
}

/// Shows the two diagnostic switches doing their job.
Future<void> _showDiagnostics(Relay relay) async {
  try {
    await relay.diagnostics();
    stdout.writeln('diagnostics answered without opt-in, which it should not');
  } on RelayException catch (error) {
    stdout.writeln('diagnostics refused before opt-in: '
        '${error.reason?.name ?? 'unknown code'}');
  }

  final report = await relay.diagnostics(enable: true);
  stdout.writeln('diagnostics enabled');
  stdout.writeln('  revision:          ${report.revision}');
  stdout.writeln('  classes:           ${report.classes.length}');
  stdout.writeln('  live targets:      ${report.targetCount}');
  stdout.writeln('  symbols resolved:  ${report.resolvedSymbolCount}');

  // Note what is not above, and cannot be: a path, a version, a vendor, a
  // machine identifier. The station does not collect them, so there is nothing
  // to redact and nothing to leak.
  stdout.writeln('  identify switch:   '
      '${report.identificationEnabled ? 'ON' : 'off (default)'}');
}
