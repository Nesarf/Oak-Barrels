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
 * Build: gcc -O2 -o port-keeper.exe port-keeper.c -lwinmm
 */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>

typedef void *VM_PORT;
typedef void(CALLBACK *VM_CALLBACK)(VM_PORT, unsigned char *, unsigned long,
                                    unsigned long long);
typedef VM_PORT (*VM_CREATE)(wchar_t *, VM_CALLBACK, unsigned long long,
                             unsigned long, unsigned long);
typedef void (*VM_SEND)(VM_PORT, unsigned char *, unsigned long);

static VM_PORT g_port = NULL;
static VM_SEND g_send = NULL;

static void CALLBACK onPortData(VM_PORT port, unsigned char *data, unsigned long length,
                                unsigned long long instance) {
  (void)port;
  (void)instance;
  if (g_port == NULL || g_send == NULL) return;
  g_send(g_port, data, length);
}

int main(int argc, char **argv) {
  const int seconds = argc > 1 ? atoi(argv[1]) : 120;

  HMODULE dll = LoadLibraryW(L"teVirtualMIDI64.dll");
  if (dll == NULL) {
    printf("load failed: %lu\n", GetLastError());
    return 1;
  }

  VM_CREATE create = (VM_CREATE)GetProcAddress(dll, "virtualMIDICreatePortEx2");
  g_send = (VM_SEND)GetProcAddress(dll, "virtualMIDISendData");
  if (create == NULL || g_send == NULL) {
    printf("exports missing\n");
    return 1;
  }

  g_port = create(L"oak-barrels", onPortData, 0, 65536, 0);
  if (g_port == NULL) {
    printf("create failed: %lu\n", GetLastError());
    return 1;
  }

  printf("port oak-barrels is up\n");
  fflush(stdout);

  Sleep((DWORD)seconds * 1000);

  printf("port closing\n");
  return 0;
}
