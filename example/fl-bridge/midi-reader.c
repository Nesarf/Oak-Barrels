/* The other end of the bridge, standing in for FL Studio: opens the virtual
 * port the shim writes to and prints whatever arrives.
 *
 * This is what FL's controller script will see as MIDI. Getting the bytes here
 * proves the shim side; getting them into FL is the script's job.
 *
 * Build: gcc -O2 -o midi-reader.exe midi-reader.c -lwinmm
 */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>

#define SYSEX_BYTES 1024

static HMIDIIN g_in = NULL;
static char g_sysex[SYSEX_BYTES];
static MIDIHDR g_header;

static void CALLBACK onMidiIn(HMIDIIN handle, UINT message, DWORD_PTR instance,
                              DWORD_PTR parameter1, DWORD_PTR parameter2) {
  (void)handle;
  (void)instance;
  (void)parameter2;

  if (message == MIM_DATA) {
    const unsigned char *bytes = (const unsigned char *)parameter1;
    printf("short %02X %02X %02X\n", bytes[0], bytes[1], bytes[2]);
    fflush(stdout);
    return;
  }

  if (message == MIM_LONGDATA) {
    MIDIHDR *header = (MIDIHDR *)parameter1;
    const unsigned char *bytes = (const unsigned char *)header->lpData;
    printf("sysex %lu bytes:", (unsigned long)header->dwBytesRecorded);
    for (DWORD i = 0; i < header->dwBytesRecorded; ++i) {
      printf(" %02X", bytes[i]);
    }
    printf("\n");
    fflush(stdout);
    midiInAddBuffer(g_in, header, sizeof(MIDIHDR));
  }
}

int main(int argc, char **argv) {
  const char *wanted = argc > 1 ? argv[1] : "oak-barrels";
  const UINT deviceCount = midiInGetNumDevs();
  UINT chosen = 0xFFFFFFFFu;

  for (UINT i = 0; i < deviceCount; ++i) {
    MIDIINCAPSA caps;
    if (midiInGetDevCapsA(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR) continue;
    printf("device %u: %s\n", i, caps.szPname);
    if (strcmp(caps.szPname, wanted) == 0) chosen = i;
  }

  if (chosen == 0xFFFFFFFFu) {
    printf("no device named %s\n", wanted);
    return 1;
  }

  if (midiInOpen(&g_in, chosen, (DWORD_PTR)onMidiIn, 0, CALLBACK_FUNCTION) !=
      MMSYSERR_NOERROR) {
    printf("midiInOpen failed\n");
    return 1;
  }

  g_header.lpData = g_sysex;
  g_header.dwBufferLength = SYSEX_BYTES;
  g_header.dwFlags = 0;
  midiInPrepareHeader(g_in, &g_header, sizeof(g_header));
  midiInAddBuffer(g_in, &g_header, sizeof(g_header));

  if (midiInStart(g_in) != MMSYSERR_NOERROR) {
    printf("midiInStart failed\n");
    return 1;
  }

  printf("listening on %s\n", wanted);
  fflush(stdout);

  Sleep(30000);

  midiInStop(g_in);
  midiInReset(g_in);
  midiInClose(g_in);
  printf("stopped\n");
  return 0;
}
