/// End to end: a real host process talking to a real station process.
///
/// The other suites test each side against a fake. This one spawns the actual
/// binary and speaks the actual protocol over actual pipes, because two fakes
/// can agree with each other about a protocol neither end implements.
///
/// Skipped when the station has not been built, so the Dart suite still passes
/// on a machine with no native toolchain. CI runs this file in the job that
/// builds the station first and points OAK_BARRELS_STATION at the result.
library;

import 'dart:io';

import 'package:oak_barrels/oak_barrels.dart';
import 'package:test/test.dart';

/// Locates the station binary, or null when it has not been built.
String? findStation() {
  final override = Platform.environment['OAK_BARRELS_STATION'];
  if (override != null && override.isNotEmpty) {
    return File(override).existsSync() ? override : null;
  }

  final name = Platform.isWindows ? 'oak-barrels.exe' : 'oak-barrels';
  for (final candidate in <String>['build/$name', '../build/$name']) {
    if (File(candidate).existsSync()) return candidate;
  }
  return null;
}

/// Connects to a pipe the station has been asked to create, retrying while the
/// process starts.
///
/// The station creates the pipe before waiting on it, but it is still a process
/// being launched, so the first attempt can arrive too early. Retrying beats
/// guessing at a delay, which is either flaky or slow and usually both.
Future<NamedPipeTransport> _connectPipe(String name) async {
  final deadline = DateTime.now().add(const Duration(seconds: 15));

  while (true) {
    try {
      return await NamedPipeTransport.connect(name);
    } on SocketException {
      if (DateTime.now().isAfter(deadline)) rethrow;
      await Future<void>.delayed(const Duration(milliseconds: 50));
    }
  }
}

void main() {
  final station = findStation();
  final skipReason = station == null
      ? 'the station is not built; run: cmake -S . -B build && cmake --build build'
      : null;

  /// Spawns a station and completes the handshake.
  Future<Relay> connected() async {
    final relay = await Relay.spawn(station!);
    await relay.negotiate();
    return relay;
  }

  group('end to end', () {
    test('negotiates revision 1 over a real pipe', () async {
      final relay = await connected();
      addTearDown(relay.dispose);

      expect(relay.revision, 1);
    });

    test('reports no capability classes, because it has no engine', () async {
      final relay = await connected();
      addTearDown(relay.dispose);

      final caps = await relay.capabilities();
      expect(caps.classes, isEmpty);
      expect(caps.limits, isEmpty);
      expect(caps.bulk, isFalse);
    });

    test('refuses to open a target, and says why with a reason code', () async {
      final relay = await connected();
      addTearDown(relay.dispose);

      // Refusing is the correct behaviour for a station with no engine behind
      // the pipe. Advertising a class it cannot honour would be the
      // over-claiming that protocol section 10 forbids.
      await expectLater(
        relay.open('emitter', name: 'ui'),
        throwsA(isA<RelayException>()
            .having((error) => error.reason, 'reason', ReasonCode.noEngine)),
      );
    });

    test('close is idempotent for a handle that was never opened', () async {
      final relay = await connected();
      addTearDown(relay.dispose);

      await expectLater(relay.close(999), completes);
    });

    test('diagnostics are refused until the host opts in', () async {
      final relay = await connected();
      addTearDown(relay.dispose);

      await expectLater(
        relay.diagnostics(),
        throwsA(isA<RelayException>().having(
            (error) => error.reason, 'reason', ReasonCode.diagDisabled)),
      );
    });

    test('opting in yields a report that identifies nothing', () async {
      final relay = await connected();
      addTearDown(relay.dispose);

      final report = await relay.diagnostics(enable: true);
      expect(report.diagnosticsEnabled, isTrue);
      // Identification is a separate switch, and the station never turns it on
      // for itself.
      expect(report.identificationEnabled, isFalse);
      expect(report.revision, 1);
      expect(report.classes, isEmpty);
      expect(report.resolvedSymbolCount, 0);
    });

    test('shuts down cleanly', () async {
      final relay = await connected();
      await expectLater(relay.shutdown(), completes);
    });

    test('says nothing about an engine when asked for its own version',
        () async {
      final result = await Process.run(station!, <String>['--version']);
      expect(result.exitCode, 0);

      final lines = (result.stdout as String).trim().split('\n');
      // One line: the station's own version. Nothing about the machine, and
      // nothing about an engine, because it has not looked at one yet.
      expect(lines, hasLength(1));
      expect(lines.single, startsWith('oak-barrels '));
    });

    test('looks only where the host told it to, and still claims nothing',
        () async {
      final root = Directory.systemTemp.createTempSync('oak-e2e-scan-');
      addTearDown(() {
        if (root.existsSync()) root.deleteSync(recursive: true);
      });

      // A file that is a loadable module by container format and nothing more.
      File('${root.path}/candidate').writeAsBytesSync(
        <int>[0x7f, 0x45, 0x4c, 0x46, 2, 1, 1, 0],
      );
      File('${root.path}/notes.txt').writeAsStringSync('not a module');

      final relay = await Relay.spawn(
        station!,
        arguments: <String>['--search-root', root.path],
      );
      addTearDown(relay.dispose);
      await relay.negotiate();

      // It looked, and it still offers nothing. Recognising a container format
      // is not the same as knowing a calling convention, and claiming a class
      // from a format alone is how a relay starts crashing on user machines.
      expect((await relay.capabilities()).classes, isEmpty);

      final report = await relay.diagnostics(enable: true);
      expect(report.filesExamined, 2); // the module, and the notes file
      expect(report.candidatesFound, 1);
      expect(report.discoveryTruncated, isFalse);
    });

    test('never touches the filesystem when no root was nominated', () async {
      final relay = await connected();
      addTearDown(relay.dispose);

      final report = await relay.diagnostics(enable: true);
      expect(report.filesExamined, 0);
      expect(report.candidatesFound, 0);
    });

    test('serves a unix socket when the host names one', () async {
      // The platform's equivalent on Windows is a named pipe, which the test
      // below covers. Neither transport exists on the other platform.
      if (Platform.isWindows) return;

      final directory = Directory.systemTemp.createTempSync('oak-e2e-socket-');
      addTearDown(() {
        if (directory.existsSync()) directory.deleteSync(recursive: true);
      });
      final socketPath = '${directory.path}/station.sock';

      final process = await Process.start(
        station!,
        <String>['--listen', socketPath],
      );
      addTearDown(process.kill);

      // The station binds before it can be connected to, so wait for the socket
      // to appear rather than guessing at a delay.
      final deadline = DateTime.now().add(const Duration(seconds: 15));
      while (!File(socketPath).existsSync()) {
        if (DateTime.now().isAfter(deadline)) {
          fail('the station never created its socket');
        }
        await Future<void>.delayed(const Duration(milliseconds: 50));
      }

      final relay = await Relay.attach(
        await UnixSocketTransport.connect(socketPath),
      );

      expect(await relay.negotiate(), 1);
      expect((await relay.capabilities()).classes, isEmpty);
      await relay.shutdown();
    });

    test('serves a named pipe when the host names one', () async {
      if (!Platform.isWindows) return;

      // A fresh name per run. Pipe names are machine-wide, so a stale one from
      // an earlier run would be connected to instead of this station's.
      final pipeName = 'oak-e2e-pipe-${DateTime.now().microsecondsSinceEpoch}';

      final process = await Process.start(
        station!,
        <String>['--pipe', pipeName],
      );
      addTearDown(process.kill);

      final relay = await Relay.attach(await _connectPipe(pipeName));

      expect(await relay.negotiate(), 1);
      expect((await relay.capabilities()).classes, isEmpty);
      await relay.shutdown();
    });

    test('refuses to serve two transports at once', () async {
      final result = await Process.run(
        station!,
        <String>['--listen', 'a.sock', '--pipe', 'a-pipe'],
      );
      expect(result.exitCode, 2);
    });

    test('rejects an argument it does not understand', () async {
      final result = await Process.run(station!, <String>['--frobnicate']);
      expect(result.exitCode, 2);
    });
  }, skip: skipReason);
}
