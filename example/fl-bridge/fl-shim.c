/* An engine-side shim for Oak-Barrels: the relay binds these symbols, and what
 * arrives here leaves again as MIDI on the oak-barrels port.
 *
 * The shim does not own that port. A long-lived keeper does, because the relay
 * station is a one-session process and would take the port down with it. This
 * module opens the port with midiOut and writes into it; whoever reads the
 * port afterwards -- FL Studio, a listener -- sees the bytes.
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

#define PORT_NAME "oak-barrels"

static HMIDIOUT g_out = NULL;

static void logLine(const char *text) {
  FILE *file = fopen("E:\\DaShaoHuo\\cache\\tmp\\fl-bridge\\shim.log", "a");
  if (file == NULL) return;
  fprintf(file, "%s\n", text);
  fclose(file);
}

static void logResult(const char *what, unsigned long value) {
  char buffer[128];
  snprintf(buffer, sizeof(buffer), "%s: %lu", what, value);
  logLine(buffer);
}

/* Opens the port the keeper owns, once. Missing port is reported once and then
 * retried on the next call, because the keeper may start after the relay. */
static void ensureOut(void) {
  if (g_out != NULL) return;

  const UINT count = midiOutGetNumDevs();
  for (UINT i = 0; i < count; ++i) {
    MIDIOUTCAPSA caps;
    if (midiOutGetDevCapsA(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR) continue;
    if (strcmp(caps.szPname, PORT_NAME) != 0) continue;

    if (midiOutOpen(&g_out, i, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR) {
      logLine("port opened for writing");
    } else {
      logResult("midiOutOpen failed", GetLastError());
    }
    return;
  }

  logLine("port not found yet");
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
  midiOutLongMsg(g_out, &header, sizeof(header));
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
