/* An engine-side shim for Oak-Barrels: the relay binds these symbols, and what
 * arrives here leaves again as MIDI on the oak-barrels port.
 *
 * The shim does not own that port. A long-lived keeper does, because the relay
 * station is a one-session process and would take the port down with it. If the
 * keeper is not running when the relay first calls in, the shim starts it: the
 * usual order is DSH driving the relay, so nothing should have to be started by
 * hand before that.
 *
 * Exported shapes, matching the probe profile:
 *   oak_shim_post(const char*)  -- POST, the action name
 *   oak_shim_set(double)        -- SET, a value in [0, 1]
 *
 * Build: gcc -shared -O2 -o oak-shim.dll fl-shim.c -lwinmm
 */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define PORT_NAME "oak-barrels"
#define KEEPER_NAME L"port-keeper.exe"

static HMIDIOUT g_out = NULL;

/* The module's own handle, which is not the same as the process's: the relay
 * loads this dll into its own address space. */
static HMODULE selfModule(void) {
  HMODULE module = NULL;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                     (LPCWSTR)&selfModule, &module);
  return module;
}

/* The log sits beside the dll, because a module loaded by somebody else's
 * process has no console to complain on. */
static void logLine(const char *text) {
  wchar_t modulePath[MAX_PATH];
  HMODULE module = selfModule();
  if (module == NULL) return;
  if (GetModuleFileNameW(module, modulePath, MAX_PATH) == 0) return;

  wchar_t *slash = wcsrchr(modulePath, L'\\');
  if (slash == NULL) return;
  *(slash + 1) = L'\0';

  char directory[MAX_PATH];
  WideCharToMultiByte(CP_ACP, 0, modulePath, -1, directory, MAX_PATH, NULL, NULL);

  char full[MAX_PATH + 32];
  snprintf(full, sizeof(full), "%sshim.log", directory);

  FILE *file = fopen(full, "a");
  if (file == NULL) return;
  fprintf(file, "%s\n", text);
  fclose(file);
}

static void logResult(const char *what, unsigned long value) {
  char buffer[128];
  snprintf(buffer, sizeof(buffer), "%s: %lu", what, value);
  logLine(buffer);
}

/* One attempt to open the port by name. */
static int tryOpenPort(void) {
  const UINT count = midiOutGetNumDevs();
  for (UINT i = 0; i < count; ++i) {
    MIDIOUTCAPSA caps;
    if (midiOutGetDevCapsA(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR) continue;
    if (strcmp(caps.szPname, PORT_NAME) != 0) continue;

    if (midiOutOpen(&g_out, i, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR) {
      logLine("port opened for writing");
      return 1;
    }
    logResult("midiOutOpen failed", GetLastError());
    return 0;
  }
  return 0;
}

/* Starts the keeper that sits beside this dll, so the port exists after a short
 * wait. Detached on purpose: the keeper must outlive whichever process the
 * relay happens to be running in. */
static void startKeeper(void) {
  wchar_t modulePath[MAX_PATH];
  HMODULE module = selfModule();
  if (module == NULL) return;
  if (GetModuleFileNameW(module, modulePath, MAX_PATH) == 0) return;

  wchar_t *slash = wcsrchr(modulePath, L'\\');
  if (slash == NULL) return;
  *(slash + 1) = L'\0';

  wchar_t keeper[MAX_PATH];
  _snwprintf(keeper, MAX_PATH, L"%s%s", modulePath, KEEPER_NAME);
  keeper[MAX_PATH - 1] = L'\0';

  wchar_t commandLine[MAX_PATH + 8];
  _snwprintf(commandLine, MAX_PATH + 8, L"\"%s\" 0", keeper);
  commandLine[MAX_PATH + 7] = L'\0';

  STARTUPINFOW startup;
  PROCESS_INFORMATION process;
  ZeroMemory(&startup, sizeof(startup));
  ZeroMemory(&process, sizeof(process));
  startup.cb = sizeof(startup);

  if (CreateProcessW(keeper, commandLine, NULL, NULL, FALSE, 0, NULL, NULL, &startup,
                     &process)) {
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    logLine("keeper started on demand");
  } else {
    logResult("keeper start failed", GetLastError());
  }
}

/* Opens the port, starting the keeper if that is what it takes. */
static void ensureOut(void) {
  if (g_out != NULL) return;
  if (tryOpenPort()) return;

  logLine("port not there, starting the keeper");
  startKeeper();
  Sleep(700);

  if (tryOpenPort()) return;
  logLine("port still not found");
}

/* POST: the action name travels as a system-exclusive message, so the payload
 * is readable on the far side without agreeing on a note table up front. */
__declspec(dllexport) void oak_shim_post(const char *name) {
  if (name == NULL) return;
  ensureOut();
  if (g_out == NULL) return;

  unsigned char payload[256];
  const size_t length = strlen(name);
  const size_t usable = length > sizeof(payload) - 3 ? sizeof(payload) - 3 : length;

  payload[0] = 0xF0; /* start of exclusive */
  payload[1] = 0x7D; /* non-commercial identifier */
  memcpy(payload + 2, name, usable);
  payload[2 + usable] = 0xF7; /* end of exclusive */

  MIDIHDR header;
  memset(&header, 0, sizeof(header));
  header.lpData = (LPSTR)payload;
  header.dwBufferLength = (DWORD)(usable + 3);

  if (midiOutPrepareHeader(g_out, &header, sizeof(header)) != MMSYSERR_NOERROR) {
    logLine("prepare failed");
    return;
  }

  const MMRESULT queued = midiOutLongMsg(g_out, &header, sizeof(header));
  if (queued != MMSYSERR_NOERROR) logResult("longMsg failed", queued);

  /* The driver sends asynchronously and marks the header when it is finished.
   * Releasing before that is a race the relay cannot see, so wait for the flag
   * with a bound: a stuck driver must not stall the session. */
  for (int attempt = 0; attempt < 200; ++attempt) {
    if ((header.dwFlags & MHDR_DONE) != 0) break;
    Sleep(1);
  }
  midiOutUnprepareHeader(g_out, &header, sizeof(header));

  logResult("posted bytes", (unsigned long)(usable + 3));
}

/* SET: one control-change message, the value quantised the way MIDI expects. */
__declspec(dllexport) void oak_shim_set(double value) {
  ensureOut();
  if (g_out == NULL) return;

  int scaled = (int)(value * 127.0 + 0.5);
  if (scaled < 0) scaled = 0;
  if (scaled > 127) scaled = 127;

  const DWORD message = 0x000000B0u | (0x00u << 8) | ((DWORD)scaled << 16);
  midiOutShortMsg(g_out, message);
  logResult("set value", (unsigned long)scaled);
}

/* A diagnostic the host side can read back. */
__declspec(dllexport) int oak_shim_ready(void) { return g_out != NULL ? 1 : 0; }
