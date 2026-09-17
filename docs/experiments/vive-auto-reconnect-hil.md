# VIVE auto-reconnect HIL procedure

Branch: `experiment/vive-auto-reconnect`

This document records the next hardware validation step for the Hub-bypass reconnect experiment. Keep the failed runtime re-arm commit in history; continue with forward commits only.

## Baseline

Known experiment history before this diagnostic change:

- `8df0f91bb82141b7ad97de3ad1c108e0fbe19080`
  - cold-start `RestartRf`
  - hardware result: auto reconnect after normal tracker power-on succeeded
- `08290bb67dfbc6eaf6559178c8bad89cb9a5640d`
  - re-sends the same cold-start `RestartRf` after `DeviceUnpaired`
  - hardware result: runtime reconnect failed; manual pair also failed until the dongle was physically re-enumerated

Do not treat `08290bb` as a completed reconnect implementation.

## Purpose of this revision

Add passive parsing and display for dongle RF status reports (`0x1D` / `0x1E`) without changing the current RestartRf behavior.

The five 32-bit slot values are deliberately exposed as raw hexadecimal values only. Their bit meanings remain reverse-engineering observations and must not be promoted to stable protocol semantics.

Expected probe line:

```text
[rf-status] report=0x1e tag=0x.. header=0x.... slot0=0x........ slot1=0x........ slot2=0x........ slot3=0x........ slot4=0x........
```

Observed examples from prior captures include:

```text
paired:             0x32000008
unpaired/off:       0x0a000005
previous direct RE: 0x04000003
```

These values are evidence only, not enum definitions.

## Build

Use the existing revision test script first:

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

Then run:

```powershell
$probe = Get-Item .\build\revision-quick\Release\vut_probe.exe
& $probe.FullName
```

VIVE Hub must be fully stopped before opening the dongle.

## HIL sequence

Start from a fresh dongle state. Physically unplug/replug the official dongle before the test so the previous stuck RF state is not carried into the run.

1. Tracker OFF.
2. Start `vut_probe`.
3. Save the first `[rf-status]` line(s).
4. Short-press the tracker power button.
5. Wait at least 30 seconds.
6. Confirm the existing cold-start reconnect behavior:
   - pair event appears
   - ACK sequence appears
   - pose begins
7. Save `[rf-status]` while connected.
8. Power the tracker OFF while leaving `vut_probe` running.
9. Confirm `DeviceUnpaired`.
10. Save every `[rf-status]` line around the unpair / RestartRf transition.
11. Short-press the tracker power button.
12. Wait at least 30 seconds.
13. Save `[rf-status]` during the reconnect attempt.
14. If no reconnect occurs, hold the tracker pair button for about 2 seconds and wait again.
15. Save `[rf-status]` after the manual-pair attempt.
16. If still stuck, stop and restart only `vut_probe`; do not unplug the dongle yet.
17. Save the first `[rf-status]` after probe restart.
18. Finally unplug/replug the dongle, restart the probe, and save the recovered `[rf-status]`.

If USBPcap is available, capture the entire sequence in parallel.

## What to report

For each phase, record:

```text
phase:
tracker LED:
pair/unpair event:
pose present:
rf-status slot0..4:
manual pair attempted:
probe restart result:
dongle replug result:
```

The important comparison is the target slot value across:

```text
connected
unpair immediately after power-off
after current runtime RestartRf
tracker power-on reconnect attempt
manual-pair attempt
probe-only restart
dongle re-enumeration
```

## Pass/fail criteria for this diagnostic revision

This revision is a diagnostic PASS if:

- build/tests pass;
- normal pair/pose handling is not regressed; and
- at least one valid `0x1D`/`0x1E` report is decoded into five raw slot values when such a report is received.

It does **not** make runtime reconnect PASS.

If no `[rf-status]` line appears during the full HIL sequence, preserve the result as `NOT OBSERVED`; do not infer slot state. The next diagnostic step would be an explicit read/query path for command `0x1E`, implemented separately so it does not contaminate this passive-observation baseline.

## Next behavior experiment

Only after the RF-status baseline is captured should runtime unpair behavior be changed from the current cold-start payload to the slot-specific Hub-observed RestartRf payload.

Current observed examples:

```text
slot0 -> 02 01 01 00 00 00 00
slot2 -> 02 01 00 00 01 00 00
```

Slots 1/3/4 remain inferred until captured. Do not label the trailing five bytes as a formally confirmed slot mask.
