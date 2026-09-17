# VIVE runtime auto-reconnect: slot-specific RestartRf HIL

Branch: `experiment/vive-auto-reconnect`

Purpose: validate whether VIVE Hub's runtime unpair behavior requires a slot-specific `RestartRf` payload rather than reusing the cold-start payload.

## Evidence before this revision

Clean Hub-bypass capture: `vive_init3.pcapng`

Observed sequence for slot0:

```text
22:49:09.181  cold-start RestartRf
               02 01 00 01 01 01 01

22:49:12.867  slot0 RF status = 0x0A000005
22:49:20.194  pair event slot0
22:49:22.867  slot0 RF status = 0x32000008
22:49:28.665  unpair event slot0
22:49:28.665  runtime RestartRf (old implementation)
               02 01 00 01 01 01 01
22:49:28.670  slot0 RF status = 0x320FA008
22:49:32.868  slot0 RF status = 0x320FA008
22:49:42.868  slot0 RF status = 0x320FA008
22:49:52.868  slot0 RF status = 0x320FA008
22:50:02.868  slot0 RF status = 0x320FA008
22:50:12.869  slot0 RF status = 0x320FA008
22:50:22.869  slot0 RF status = 0x320FA008
```

No second pair event was observed after runtime unpair. Tracker restart and manual re-pair were both unsuccessful in this clean run.

Therefore the previous runtime behavior is treated as:

```text
cold-start RestartRf reused after unpair: FAIL
```

The earlier successful reconnect capture was contaminated by additional VIVE/HTC control traffic and is not sufficient evidence for Backend-only reconnect.

## Change under test

Cold-start behavior is unchanged:

```text
02 01 00 01 01 01 01
```

Runtime `DeviceUnpaired` now emits the slot-specific pattern observed from VIVE Hub captures:

```text
slot0 -> 02 01 01 00 00 00 00
slot1 -> 02 01 00 01 00 00 00   (inferred, not yet captured)
slot2 -> 02 01 00 00 01 00 00
slot3 -> 02 01 00 00 00 01 00   (inferred, not yet captured)
slot4 -> 02 01 00 00 00 00 01   (inferred, not yet captured)
```

Only slot0 and slot2 have direct capture evidence. Do not document the trailing five bytes as a formally confirmed slot mask yet.

## Build

Use the existing Windows environment.

```powershell
$cmakeDir = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
$env:Path = "$cmakeDir;$env:Path"

$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
cmd.exe /s /c "`"$vsDevCmd`" -arch=x64 -host_arch=x64 >nul && set" |
    ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') {
            Set-Item -Path "Env:$($matches[1])" -Value $matches[2]
        }
    }

powershell -ExecutionPolicy Bypass -File .\scripts\test_revision.ps1 -Mode Quick
```

Run probe:

```powershell
$probe = Get-Item .\build\revision-quick\Release\vut_probe.exe
& $probe.FullName
```

## Required HIL conditions

- VIVE Hub UI must be closed.
- HTC/VIVE background processes that can control the dongle must be stopped for the clean comparison.
- Physically unplug/replug the official dongle before the run.
- Start USBPcap before `vut_probe` if possible.
- Use one tracker first; record its slot.

## Test sequence

1. Tracker OFF.
2. Fresh dongle replug.
3. Start USBPcap.
4. Start `vut_probe`.
5. Short-press tracker power ON.
6. Wait up to 30 seconds for the known cold-start reconnect.
7. Confirm:
   - pair event
   - ACK traffic
   - pose stream
   - target slot RF status while connected
8. Leave probe running and power tracker OFF.
9. Confirm unpair event.
10. Verify USBPcap shows the runtime payload for the actual slot.
    For slot0 it must be:

```text
02 01 01 00 00 00 00
```

11. Save `[rf-status]` immediately after RestartRf.
12. Short-press tracker power ON.
13. Wait at least 30 seconds.
14. Record whether pair event reappears and pose resumes.
15. If auto reconnect does not occur, perform the normal ~2 second manual pair operation once.
16. Wait another 30 seconds.
17. Record pair event / pose / RF status.
18. Do not unplug/replug until all failed-state evidence has been captured.
19. Finally replug the dongle only to verify recovery from any stuck state.

## PASS criteria

`F-HIL-RECONNECT-02-slot-specific` is PASS only if, with VIVE/HTC control processes absent:

```text
connected
-> tracker OFF / DeviceUnpaired
-> slot-specific RestartRf sent
-> tracker normal power ON
-> pair event reappears
-> pose resumes
```

within the observation window without restarting `vut_probe` or physically re-enumerating the dongle.

Manual pair success is useful evidence but does not by itself satisfy automatic reconnect PASS.

## Failure evidence to preserve

If auto reconnect fails, record:

```text
slot:
runtime RestartRf payload:
RF status before unpair:
RF status immediately after RestartRf:
RF status at +10s:
RF status at +20s:
RF status at +30s:
pair event observed: yes/no
pose resumed: yes/no
manual pair result: pass/fail/not run
probe-only restart result: pass/fail/not run
dongle replug recovery: pass/fail/not run
tracker LED sequence:
```

Attach the corresponding `.pcapng`.

## Interpretation guardrails

- Do not change the known-working cold-start payload during this test.
- Do not infer official bit meanings from `0x32000008`, `0x320FA008`, `0x0A000005`, or related values.
- Do not call the trailing five RestartRf bytes a confirmed slot mask until all relevant slots are independently observed.
- Do not merge this experiment into `refactor/monaka-layer-separation` until runtime reconnect behavior is reproduced cleanly and repeatedly.
