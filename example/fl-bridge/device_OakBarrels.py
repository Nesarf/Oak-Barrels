# name=Oak Barrels Bridge
#
# Receives what the relay's shim sends on the virtual port and shows it in FL.
# The shim writes POST names as a system-exclusive message (F0 7D <utf-8> F7)
# and SET values as control change 0 on channel 1.
#
# This is the last link in: DSH -> relay station -> shim -> MIDI port -> here.

import device
import ui


def OnInit():
    print('oak-barrels: bridge script loaded')
    ui.setHintMsg('oak-barrels: bridge script loaded')


def OnDeInit():
    print('oak-barrels: bridge script unloaded')


def OnSysEx(event):
    data = bytes(event.sysex)
    if len(data) < 3 or data[0] != 0xF0 or data[1] != 0x7D:
        return

    payload = data[2:-1]
    try:
        name = payload.decode('utf-8', 'replace')
    except Exception:
        name = repr(payload)

    print('oak-barrels: action ' + name)
    ui.setHintMsg('oak-barrels: ' + name)
    event.handled = True


def OnMidiMsg(event):
    if event.status == 0xB0:
        print('oak-barrels: value ' + str(event.data2))
        ui.setHintMsg('oak-barrels: value ' + str(event.data2))
        event.handled = True


def OnRefresh(flags):
    return 0
