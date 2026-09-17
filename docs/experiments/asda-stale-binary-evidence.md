# `asda.pcapng` reconnect capture — stale binary evidence

Date analyzed: 2026-09-18

This capture was intended to validate the slot-specific runtime `RestartRf` experiment, but the USB traffic proves that the executable under test still used the previous cold-start payload after runtime unpair.

Therefore this capture is **not** evidence for or against commit `1c8f8a599dc92062bf44e3b79503f921d64636dd`.

## Relevant sequence

Tracker is slot0 (`23:30:03:81:fe:ec`).

```text
23:03:36.109  Backend startup RestartRf
                02 01 00 01 01 01 01

23:03:40.885  RF status slot0 = 0x0A000005
23:03:48.172  pair event slot0
23:03:50.885  RF status slot0 = 0x32000008

23:04:01.516  unpair event slot0
23:04:01.516  runtime RestartRf actually transmitted
                02 01 00 01 01 01 01

23:04:10.886  RF status slot0 = 0x320FA008
23:04:20.887  RF status slot0 = 0x320FA008
23:04:30.887  RF status slot0 = 0x320FA008
23:04:40.887  RF status slot0 = 0x320FA008
23:04:50.888  RF status slot0 = 0x320FA008
23:05:00.888  RF status slot0 = 0x320FA008
23:05:10.888  RF status slot0 = 0x320FA008
23:05:20.888  RF status slot0 = 0x320FA008
23:05:30.889  RF status slot0 = 0x320FA008
```

No second pair event was observed in this interval.

## Why this is a stale-binary run

The current experiment source after commit `1c8f8a599dc92062bf44e3b79503f921d64636dd` generates runtime payloads from the device slot.

For slot0 the expected runtime payload is:

```text
02 01 01 00 00 00 00
```

The capture instead contains:

```text
02 01 00 01 01 01 01
```

which matches the previous runtime implementation in commit `08290bb67dfbc6eaf6559178c8bad89cb9a5640d`.

The most likely explanations are an older checkout or an older `vut_probe.exe` from a reused build directory. Do not infer a source-code regression from this capture.

## Required next run

Use:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run_reconnect_slot_specific_hil.ps1 -RunProbe
```

The helper refuses to run if HEAD does not contain the slot-specific implementation commit and configures a dedicated CMake build directory with `--fresh`.

For a slot0 run, do not judge reconnect success/failure until USBPcap confirms the runtime `DeviceUnpaired` command is exactly:

```text
02 01 01 00 00 00 00
```

Cold-start remains intentionally unchanged:

```text
02 01 00 01 01 01 01
```

## Classification

```text
old cold-start payload reused at runtime: FAIL evidence retained
slot-specific runtime RestartRf: NOT TESTED by this capture
stale binary/build contamination: CONFIRMED by transmitted payload
```
