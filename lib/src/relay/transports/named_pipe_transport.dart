/// A Windows named pipe transport, reached through `dart:ffi`.
///
/// Dart exposes no named pipe API, and its file API cannot stand in: `FileMode`
/// has no read-write mode, and `RandomAccessFile` seeks, which a pipe does not
/// support. So this file talks to the platform directly.
///
/// It adds no package for that. `package:ffi` would supply allocation and
/// UTF-16 conversion; both are short against `LocalAlloc` and the fact that a
/// Dart string is already a sequence of UTF-16 code units. A relay that depends
/// on nothing should not start by depending on three lines of convenience.
///
/// Reading happens on its own isolate, because waiting for a frame must not
/// block whatever isolate the host's UI runs on. That worker asks whether data
/// is waiting and sleeps briefly when it is not, rather than sitting in a
/// blocking read. The trade is deliberate and worth stating: a short poll costs
/// a wake-up every few milliseconds, and it buys two things a blocking read
/// cannot give. A worker blocked inside a system call cannot be killed, so the
/// process would refuse to exit while it waited; and on a pipe whose far end has
/// not finished connecting, a blocking read can fail outright rather than wait.
library;

import 'dart:async';
import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';
import 'dart:typed_data';

import '../transport.dart';

// ------------------------------------------------------------------ bindings

// Top-level finals are initialised on first read, so importing this file opens
// no platform library by itself. Nothing below is touched until connect() has
// established that this is a platform that has named pipes.
final DynamicLibrary _kernel32 = DynamicLibrary.open('kernel32.dll');

typedef _CreateFileWNative = IntPtr Function(
  Pointer<Uint16>,
  Uint32,
  Uint32,
  Pointer<Void>,
  Uint32,
  Uint32,
  IntPtr,
);
typedef _CreateFileWDart = int Function(
  Pointer<Uint16>,
  int,
  int,
  Pointer<Void>,
  int,
  int,
  int,
);

typedef _ReadFileNative = Int32 Function(
  IntPtr,
  Pointer<Uint8>,
  Uint32,
  Pointer<Uint32>,
  Pointer<Void>,
);
typedef _ReadFileDart = int Function(
  int,
  Pointer<Uint8>,
  int,
  Pointer<Uint32>,
  Pointer<Void>,
);

typedef _WriteFileNative = Int32 Function(
  IntPtr,
  Pointer<Uint8>,
  Uint32,
  Pointer<Uint32>,
  Pointer<Void>,
);
typedef _WriteFileDart = int Function(
  int,
  Pointer<Uint8>,
  int,
  Pointer<Uint32>,
  Pointer<Void>,
);

typedef _PeekNamedPipeNative = Int32 Function(
  IntPtr,
  Pointer<Void>,
  Uint32,
  Pointer<Uint32>,
  Pointer<Uint32>,
  Pointer<Void>,
);
typedef _PeekNamedPipeDart = int Function(
  int,
  Pointer<Void>,
  int,
  Pointer<Uint32>,
  Pointer<Uint32>,
  Pointer<Void>,
);

typedef _CloseHandleNative = Int32 Function(IntPtr);
typedef _CloseHandleDart = int Function(int);

typedef _LocalAllocNative = IntPtr Function(Uint32, IntPtr);
typedef _LocalAllocDart = int Function(int, int);

typedef _LocalFreeNative = IntPtr Function(IntPtr);
typedef _LocalFreeDart = int Function(int);

final _createFileW = _kernel32
    .lookupFunction<_CreateFileWNative, _CreateFileWDart>('CreateFileW');
// No ReadFile binding here: reading happens on the worker isolate, which looks
// the symbol up for itself because a function pointer cannot be sent to one.
final _writeFile =
    _kernel32.lookupFunction<_WriteFileNative, _WriteFileDart>('WriteFile');
final _closeHandle = _kernel32
    .lookupFunction<_CloseHandleNative, _CloseHandleDart>('CloseHandle');
final _localAlloc =
    _kernel32.lookupFunction<_LocalAllocNative, _LocalAllocDart>('LocalAlloc');
final _localFree =
    _kernel32.lookupFunction<_LocalFreeNative, _LocalFreeDart>('LocalFree');

const int _genericRead = 0x80000000;
const int _genericWrite = 0x40000000;
const int _openExisting = 3;
const int _lptr = 0x0040;
const int _invalidHandle = -1;

const int _chunkBytes = 64 * 1024;

/// How long the worker sleeps when nothing is waiting.
///
/// Short enough that a host cannot perceive the added latency on a
/// request-and-reply protocol, long enough not to spin a core.
const Duration _pollInterval = Duration(milliseconds: 2);

const String _pipePrefix = r'\\.\pipe\';

/// The platform's own prefix, added when the host supplies a bare name.
///
/// Adding a prefix the operating system requires is translating, not inventing:
/// the part that identifies this pipe still comes from the host.
String fullPipeName(String name) =>
    name.startsWith(_pipePrefix) ? name : _pipePrefix + name;

/// Allocates a zeroed block, or throws. The caller frees it.
Pointer<Uint8> _allocate(int bytes) {
  final block = _localAlloc(_lptr, bytes);
  if (block == 0) throw StateError('no memory available for a pipe buffer');
  return Pointer<Uint8>.fromAddress(block);
}

/// Copies a Dart string into a null-terminated UTF-16 buffer.
Pointer<Uint16> _copyUtf16(String text) {
  final units = text.codeUnits;
  final raw = _localAlloc(_lptr, (units.length + 1) * 2);
  if (raw == 0) throw StateError('no memory available for a pipe name');

  final buffer = Pointer<Uint16>.fromAddress(raw);
  for (var index = 0; index < units.length; index++) {
    buffer[index] = units[index];
  }
  buffer[units.length] = 0;
  return buffer;
}

// -------------------------------------------------------------- read worker

/// Reads until the pipe ends, posting whatever arrives to [out].
///
/// This runs on its own isolate because waiting must not block the caller. The
/// bindings are looked up again here rather than sent across, because a
/// function pointer is not something an isolate can receive.
Future<void> _readLoop(List<Object> arguments) async {
  final handle = arguments[0] as int;
  final out = arguments[1] as SendPort;

  final kernel32 = DynamicLibrary.open('kernel32.dll');
  final peekNamedPipe =
      kernel32.lookupFunction<_PeekNamedPipeNative, _PeekNamedPipeDart>(
          'PeekNamedPipe');
  final readFile =
      kernel32.lookupFunction<_ReadFileNative, _ReadFileDart>('ReadFile');
  final localAlloc =
      kernel32.lookupFunction<_LocalAllocNative, _LocalAllocDart>('LocalAlloc');
  final localFree =
      kernel32.lookupFunction<_LocalFreeNative, _LocalFreeDart>('LocalFree');

  final buffer = Pointer<Uint8>.fromAddress(localAlloc(_lptr, _chunkBytes));
  final counted = Pointer<Uint32>.fromAddress(localAlloc(_lptr, 4));
  final available = Pointer<Uint32>.fromAddress(localAlloc(_lptr, 4));

  try {
    while (true) {
      // Ask first, read second. A read issued before the far end has finished
      // connecting reports a failure rather than waiting, so the question is
      // asked before the answer is demanded.
      final peeked =
          peekNamedPipe(handle, nullptr, 0, nullptr, available, nullptr);
      if (peeked == 0) break; // the station is gone

      final waiting = available.value;
      if (waiting == 0) {
        sleep(_pollInterval);
        continue;
      }

      final wanted = waiting < _chunkBytes ? waiting : _chunkBytes;
      final ok = readFile(handle, buffer, wanted, counted, nullptr);
      if (ok == 0) break;

      final received = counted.value;
      if (received == 0) break;

      out.send(Uint8List.fromList(buffer.asTypedList(received)));
    }
  } finally {
    localFree(buffer.address);
    localFree(counted.address);
    localFree(available.address);
    // A null message is the end of the stream, and the only one sent.
    out.send(null);
  }
}

// ----------------------------------------------------------------- transport

class NamedPipeTransport implements RelayTransport {
  NamedPipeTransport._(this._handle, this._incoming, this._worker);

  final int _handle;
  final Stream<List<int>> _incoming;
  final Isolate _worker;
  bool _closed = false;

  /// Connects to the pipe [name], which the station created with `--pipe`.
  ///
  /// Throws [UnsupportedError] where named pipes do not exist, and
  /// [SocketException] when nothing is listening under that name.
  static Future<NamedPipeTransport> connect(String name) async {
    if (!Platform.isWindows) {
      throw UnsupportedError('named pipes are not available on this platform');
    }
    if (name.isEmpty) {
      throw ArgumentError.value(name, 'name', 'a pipe name is required');
    }

    final wideName = _copyUtf16(fullPipeName(name));
    final int handle;
    try {
      handle = _createFileW(
        wideName,
        _genericRead | _genericWrite,
        0,
        nullptr,
        _openExisting,
        0,
        0,
      );
    } finally {
      _localFree(wideName.address);
    }

    if (handle == _invalidHandle || handle == 0) {
      throw const SocketException('could not connect to the named pipe');
    }

    final incoming = ReceivePort();
    final errors = ReceivePort();
    final worker = await Isolate.spawn<List<Object>>(
      _readLoop,
      <Object>[handle, incoming.sendPort],
      onError: errors.sendPort,
      onExit: errors.sendPort,
      errorsAreFatal: true,
    );

    // Both the worker's own end-of-stream and the isolate's exit try to close
    // this, and whichever arrives second must not throw.
    final controller = StreamController<List<int>>();
    incoming.listen((Object? message) {
      if (message is Uint8List) {
        controller.add(message);
      } else if (!controller.isClosed) {
        controller.close();
      }
    });
    errors.listen((Object? message) {
      if (!controller.isClosed) controller.close();
    });

    return NamedPipeTransport._(handle, controller.stream, worker);
  }

  @override
  Stream<List<int>> get incoming => _incoming;

  @override
  void send(List<int> bytes) {
    if (_closed || bytes.isEmpty) return;

    final buffer = _allocate(bytes.length);
    final counted = _allocate(4);
    try {
      buffer.asTypedList(bytes.length).setAll(0, bytes);
      final written = Pointer<Uint32>.fromAddress(counted.address);
      final ok = _writeFile(_handle, buffer, bytes.length, written, nullptr);
      if (ok == 0) _closed = true;
    } finally {
      _localFree(buffer.address);
      _localFree(counted.address);
    }
  }

  @override
  Future<void> close() async {
    if (_closed) return;
    _closed = true;

    // The worker is stopped first. It only ever sits in a short sleep, so it
    // takes effect promptly -- which is the reason it polls rather than blocking
    // in a read that nothing could interrupt.
    _worker.kill(priority: Isolate.immediate);
    _closeHandle(_handle);
  }
}
