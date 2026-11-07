/* Keeps the virtual port alive for as long as the bridge is wanted.
 *
 * A teVirtualMIDI port belongs to the process that created it, and the relay
 * station is a one-session process: it exits when the host disconnects, taking
 * the port with it. FL Studio would then have nothing to select.
 *
 * So this process owns the port instead. It creates it and loops back whatever
 * arrives, which is exactly what makes it visible to both sides: writers open
 * it with midiOut, readers open it with midiIn.
 *
 * A second instance leaves immediately if the port is already owned, which is
 * what makes it safe for the shim to start one on demand.
 *
 * Build: gcc -O2 -o port-keeper.exe port-keeper.c -lwinmm
 */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>

typedef void *VM_PORT;
typedef void(CALLBACK *VM_CALLBACK)(VM_PORT, unsigned char *, unsigned long,
                                    unsigned long long);
typedef VM_PORT (*VM_CREATE)(wchar_t *, VM_CALLBACK, unsigned long long,
                             unsigned long, unsigned long);
typedef void (*VM_SEND)(VM_PORT, unsigned char *, unsigned long);

static VM_PORT g_port = NULL;
static VM_SEND g_send = NULL;

/* The log sits beside the executable, because a process started by a task or by
 * another process has no console to complain on. */
static void logLine(const char *text) {
  wchar_t modulePath[MAX_PATH];
  if (GetModuleFileNameW(NULL, modulePath, MAX_PATH) == 0) return;

  wchar_t *slash = wcsrchr(modulePath, L'\\');
  if (slash == NULL) return;
  *(slash + 1) = L'\0';

  char path[MAX_PATH];
  WideCharToMultiByte(CP_ACP, 0, modulePath, -1, path, MAX_PATH, NULL, NULL);

  char full[MAX_PATH + 32];
  snprintf(full, sizeof(full), "%skeeper.log", path);

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

static void CALLBACK onPortData(VM_PORT port, unsigned char *data, unsigned long length,
                                unsigned long long instance) {
  (void)port;
  (void)instance;
  if (g_port == NULL || g_send == NULL) return;
  g_send(g_port, data, length);
}

int main(int argc, char **argv) {
  const int seconds = argc > 1 ? atoi(argv[1]) : 120;
  logLine("keeper starting");

  HMODULE dll = LoadLibraryW(L"teVirtualMIDI64.dll");
  if (dll == NULL) {
    logResult("load failed", GetLastError());
    return 1;
  }
  logLine("driver dll loaded");

  VM_CREATE create = (VM_CREATE)GetProcAddress(dll, "virtualMIDICreatePortEx2");
  g_send = (VM_SEND)GetProcAddress(dll, "virtualMIDISendData");
  if (create == NULL || g_send == NULL) {
    logLine("exports missing");
    return 1;
  }

  /* A second instance has to be harmless when one is already running: two
   * processes cannot own the same port name, so this one leaves. */
  const UINT devices = midiOutGetNumDevs();
  for (UINT i = 0; i < devices; ++i) {
    MIDIOUTCAPSA caps;
    if (midiOutGetDevCapsA(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR) continue;
    if (strcmp(caps.szPname, "oak-barrels") == 0) {
      logLine("port already owned elsewhere");
      return 0;
    }
  }

  g_port = create(L"oak-barrels", onPortData, 0, 65536, 0);
  if (g_port == NULL) {
    logResult("create failed", GetLastError());
    return 1;
  }
  logLine("port up");

  /* Zero or a negative number means "until somebody ends this process", which
   * is what a started-on-demand instance wants: the port stays up. */
  if (seconds <= 0) {
    for (;;) {
      Sleep(60000);
    }
  }

  Sleep((DWORD)seconds * 1000);
  logLine("port closing");
  return 0;
}
