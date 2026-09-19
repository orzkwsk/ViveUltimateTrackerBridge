# ViveUltimateTrackerBridge

Development branch: `dev/vivehub-bypass`

Native backend for using VIVE Ultimate Trackers through the official USB dongle without running VIVE Hub / VIVE Streaming Hub.

## Current phase

```text
VIVE Ultimate Tracker(s)
        ↓ proprietary RF
official VIVE USB dongle
        ↓ Windows HID
VUT Backend
  device topology / control / packet decode / validity
        ↓
Normalized TrackerState
```

This phase intentionally does **not** implement Wi-Fi-only tracker transport or tracker-count-limit bypass. Those are separate future work.

## Implemented

- standalone Windows HID access to the official VIVE dongle (`VID 0x0bb4`, `PID 0x0350`);
- Hub-independent dongle feature-command transport;
- pair/unpair event parsing;
- minimal SLAM host/client bring-up over tracker ACK commands;
- tracker incoming envelope parser;
- 37/39-byte 6DoF pose parser;
- float16 decode and tracker quaternion reorder to normalized `xyzw`;
- tracking-state validity normalization;
- button-fragment accumulation;
- normalized in-process `TrackerState` snapshot/event API;
- `vut_probe` command-line hardware probe;
- protocol unit tests independent of hardware.

See `docs/INITIAL_RESEARCH.md` for protocol evidence, assumptions, and current limitations.

## Build

Windows, Visual Studio 2022 / CMake:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

No external HID library is required; the backend uses Windows HID/SetupAPI directly.

## Quick start

For normal bridge validation on Windows, use the repository-root launcher:

```cmd
Start_VIVE_Backend.cmd
```

The launcher configures/builds `vut_service` in `build/runtime` and starts it in the foreground.
It uses `config/vut-service.conf` and stores the persistent revision journal under
`%LOCALAPPDATA%\\MonakaVR\\ViveUltimateTrackerBridge\\vut-service.revision.log`.

Before first use, copy `config/vut-service.example.conf` to `config/vut-service.conf` and replace
all example source/map values. The local config is gitignored. Close VIVE Hub / VIVE Streaming Hub
before opening the official dongle. Ctrl+C stops the service.

The current service configuration still keys mappings by the full RF address accepted by the
historical service path. Do not treat that address as persistent hardware identity; if it changes,
update the local config and restart for validation. This launcher does not fabricate or persist a
hardware identity.

Normal `vut_service` startup does **not** enter dongle pairing mode. It assumes the tracker is
already paired and only observes the existing relationship. Pairing/re-pairing is an explicit
diagnostic/setup action; restarting the service must not change pairing state.

## Hardware probe

Do not run VIVE Hub at the same time because it may already own/use the same dongle interface.

```powershell
.\build\Release\vut_probe.exe
```

Expected output after a tracker connects:

```text
[pair] slot=... mac=...
[ack]  slot=... ...
[pose] slot=... status=2 pvalid=1 ovalid=1 pos=(...) q=(...)
```

For the first hardware validation, verify these cases separately:

1. one tracker: pair/connect and continuous status=2 pose;
2. cover cameras: status transitions to 3/4 and `positionValid` becomes false;
3. uncover cameras: status returns to 2;
4. tracker power-cycle: re-pair/recovery;
5. two or more trackers: one host plus client trackers, simultaneous pose stream;
6. physical 1 m translation sanity check for position units/axis signs.

## Backend boundary

The backend intentionally does not perform:

- SteamVR coordinate conversion;
- physical mount/body transforms;
- IK;
- fusion/fallback;
- generic-tracker registration.

Those belong above `TrackerState`, matching the separation already used by `PicoMotionTrackerBridge`.

## References

Protocol behavior was independently reimplemented from public reverse-engineering observations, primarily:

- `shinyquagsire23/vive_ultimate_tracker_re`
- `zengguizhang/pyvut` / `nijkah/pyvut`

No third-party source is vendored or copied into this repository.

## Task 3 common Observation boundary

The fixed Task 1 kit is now consumed by `ViveObservationAdapter`, the bounded loopback
`ObservationPublisher`, and the Windows `vut_service` executable. Existing backend/probe targets
remain available without C1 (`-DVUT_BUILD_OBSERVATION=OFF`). Full instructions, exact mapping,
RF-counter limitations, test ledger and Task 4 handoff: [Task 3](docs/task3/implementation.md).

Implemented and mock/Windows-build verified does not mean hardware verified: no Hub-free dongle,
tracker, map, 1 m translation or quaternion-axis hardware checks were run for Task 3. Battery and
all derivatives remain null. `vut-native-v1` requires a verified upper-layer conversion.
