# VIVE DebugConsole HIL harness

Branch:

```text
experiment/vive-debug-console
```

This branch is derived from `experiment/vive-auto-reconnect`. It is an
experiment-only harness and is not approved for merge into the coordinated
Monaka layer-separation branch.

## Purpose

Preserve a live/stuck dongle RF state while sending controlled recovery
commands without rebuilding/restarting the backend between every trial.

USBPcap remains the primary evidence source. Console output is secondary
diagnostic evidence.

## Safety / evidence boundary

Named commands intentionally expose only the currently useful non-destructive
RF-control candidates:

- RF status query
- PairDevice enable/disable
- RxPowerSaving enable/disable
- RestartRf
- candidate RF reboot to APP

Factory reset and the public-RE `0x06` clear-pairing candidate are deliberately
not exposed as named commands.

`rf-reboot-app` uses direct dongle command `0x1C` with payload `00 03`.
This is public-RE/USBTool-informed and remains unconfirmed vendor semantics.
Treat it as an experiment, not a stable protocol API.

The trailing five RF behavior selector bytes are not formally named a slot
mask. Slot0 and slot2 RestartRf selector positions were observed in Hub
captures. Slot1/3/4 one-hot behavior remains inferred.

## Build

Recommended one-command fresh build/test/run:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run_vive_debug_console.ps1
```

Build/test only:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run_vive_debug_console.ps1 -NoRun
```

The runner refuses to run from a branch other than
`experiment/vive-debug-console`, uses a fresh dedicated build directory,
runs `vut_protocol_tests`, and prints the resulting executable SHA-256.

Manual build from a VS developer shell, or after importing the VS environment
used by the existing HIL instructions:

```powershell
cmake --fresh -S . -B build/debug-console -A x64 -DBUILD_TESTING=ON -DVUT_BUILD_OBSERVATION=OFF
cmake --build build/debug-console --config Release --target vut_debug_console vut_protocol_tests
.\build\debug-console\Release\vut_protocol_tests.exe
.\build\debug-console\Release\vut_debug_console.exe
```

Expected marker:

```text
[build] marker=VUT_DEBUG_CONSOLE_PWS_OFF_V2
```

## Default behavior

DebugConsole starts with:

```text
policy=none
```

Therefore a `DeviceUnpaired` event does not automatically send RestartRf.
This is intentional: a stuck state can be captured and stimulated manually.

The existing `vut_probe` behavior remains unchanged because
`BackendConfig` defaults to `SlotSpecificRestartRf`.

## Commands

```text
help
query

restart <0-4|all>
pair-on <0-4|all>
pair-off <0-4|all>
pws-on <0-4|all>
pws-off <0-4|all>

rf-reboot-app

raw <cmdHex> [byteHex ...]

policy <name>
delay <milliseconds>
policy-show

snapshot
pose <on|off>
quit
```

Automatic unpair policies:

```text
none
pws-off
slot-restart
cold-restart
pws-restart
pairoff-restart
pairoff-pws-restart
rfreboot
rfreboot-restart
```

## Recommended stuck-state workflow

1. Stop VIVE Hub and other HTC/VIVE processes that could control the dongle.
2. Start USBPcap.
3. Start `vut_debug_console.exe`; verify the build marker.
4. Confirm `policy-show` reports `none`.
5. Establish normal tracking.
6. Power the tracker off and wait for the unpair/stuck state.
7. Run `query` and record RF status before any recovery command.
8. Try exactly one recovery candidate.
9. Run `query` again immediately and after the desired observation delay.
10. Power the tracker on only when the test definition calls for it.
11. Preserve the PCAP even when the trial fails.

Useful first-pass single-variable experiments:

```text
pws-off 0
restart 0
pair-off 0
rf-reboot-app
```

Do not chain multiple candidates in one run until the single-command effect is
understood; otherwise the command that changed the state cannot be isolated.

## Raw command path

`raw` exists for firmware-analysis-driven experiments whose final direct-HID
format is not yet promoted to a named command.

Example syntax only:

```text
raw 1e
raw 1d 02 01 01 00 00 00 00
```

Raw send bypasses named-command guardrails. Do not use factory reset, flash,
erase, bootloader/update, or pairing-DB destructive candidates casually.

## Logging

Each debug send emits a diagnostic record containing:

```text
command
payload
HidD_SetFeature send status
matching feature-response status
response bytes when present
error/note
```

Pair/unpair/RF status/ACK events continue to be emitted by the backend.

For final HIL decisions, use the USBPcap bytes and event sequence rather than
assuming that a successful feature write means the RF subsystem accepted the
semantic operation.


## Automatic PWS-off reconnect validation

HIL on 2026-09-19 observed the following sequence without RestartRf:

```text
connected          slot0=0x32000008
tracker power off
DeviceUnpaired
                   slot0=0x320fa008
RxPowerSaving(false)
                   slot0=0x0a000005
tracker power on
DevicePaired
                   slot0=0x32000008
```

The dedicated automatic policy is:

```text
policy pws-off
```

It sends exactly one RF behavior request when a DeviceUnpaired event is
decoded:

```text
command: 0x1d
payload for slot0: 01 00 01 00 00 00 00
```

No RestartRf, PairDevice, RF reboot, or destructive command is appended.

### Repeated validation procedure

1. Start USBPcap before starting the DebugConsole.
2. Start the DebugConsole and confirm the build marker.
3. Enter:

```text
policy pws-off
policy-show
pose on
```

4. Confirm `policy-show` reports `policy=pws-off`.
5. Power tracker slot0 on and wait for pair, normal RF status, and pose.
6. Power the tracker off normally.
7. Confirm the capture/log contains:
   - DeviceUnpaired;
   - exactly one `0x1d` RF behavior request with
     `01 00 01 00 00 00 00`;
   - RF status transition from `0x320fa008` to `0x0a000005`;
   - no runtime RestartRf request.
8. Power the tracker on normally without pressing the pairing action.
9. Wait up to 30 seconds and confirm:
   - DevicePaired reappears;
   - slot0 returns to `0x32xxxx08`;
   - pose resumes.
10. Repeat steps 6-9 for at least five consecutive cycles without restarting
    the DebugConsole or re-enumerating the dongle.

A cycle is a PASS only when reconnect occurs after ordinary tracker power-on
without manual pairing, backend restart, dongle re-enumeration, or additional
recovery commands.

Preserve the PCAP for any failed cycle. A failure after several successful
cycles is more informative than restarting the environment immediately.
