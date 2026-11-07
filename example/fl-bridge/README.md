# FL Studio bridge

> A worked example: the relay binds a shim, the shim writes MIDI, and a DAW
> reads it. Windows only, because both halves of the audio side are.

What runs, in order:

```
DSH (Dart client) --stdio--> oak-barrels station --probe profile--> oak-shim.dll
                                                                      |
                                                                 midiOut
                                                                      v
                                            port-keeper.exe --> oak-barrels port
                                                                      |
                                                                  midiIn
                                                                      v
                                                    FL Studio controller script
```

Nothing in the relay knows what FL Studio is. The station is told which symbols
to resolve by `profile.json`, and those symbols happen to live in a module that
speaks MIDI.

## Why a keeper

A teVirtualMIDI port belongs to the process that created it. The station serves
one session and then exits, so a port it created would go away with it.
`port-keeper` owns the port instead and loops bytes back, which is what makes
the port reachable by a writer (midiOut) and a reader (midiIn) at the same
time.

## Build

Needs a MinGW toolchain on PATH. The virtual port comes from the loopMIDI
package, which supplies `teVirtualMIDI64.dll` and its driver.

```bash
gcc -shared -O2 -o oak-shim.dll fl-shim.c -lwinmm
gcc -O2 -o port-keeper.exe port-keeper.c -lwinmm
gcc -O2 -o midi-reader.exe midi-reader.c -lwinmm
gcc -O2 -o shim-host.exe shim-host.c
```

Built modules are not committed; the repository's `.gitignore` keeps them out.

## Run it

1. `port-keeper.exe 86400` brings the port up and keeps it up for a day.
2. In FL Studio: Options, MIDI settings, enable the `oak-barrels` input and set
   its controller type to the `Oak Barrels Bridge` script.
3. Start the station against this directory, then drive it from a host:

```bash
dart run example/reference_host.dart <station> \
  --search-root example/fl-bridge \
  --probe-profile example/fl-bridge/profile.json
```

`midi-reader.exe` stands in for FL when checking the MIDI side alone: it opens
the same port with midiIn and prints what arrives.

## The script

The controller script lives on the FL side, at
`Documents\Image-Line\FL Studio\Settings\Hardware\OakBarrels\device_OakBarrels.py`.
It prints the action name to the hint line, and it is the last hop: without it
the bytes reach the port and stop there.

The wire format is deliberately small: a POST name travels as
`F0 7D <utf-8 name> F7`, and a SET value as control change 0 on channel 1.
