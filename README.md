# ViveUltimateTrackerBridge

Current integration lineage: `refactor/monaka-layer-separation`.

For current wire-v2 behavior, production defaults and diagnostic/HIL limits, see
[production integration](docs/production-integration.md) and [software release evidence](docs/release-v2.md).

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
- explicit one-shot `vut_pair` first-pairing setup tool;
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
An existing process is accepted only after executable/config/journal/options match;
this check does not establish stream or hardware health. Use `-Diagnostics` on
`scripts/start_vive_backend.ps1` for detailed logging. Default operation does not
issue persistent-loss SLAM queries; active queries require an explicit probe option.
It uses `config/vut-service.conf` and stores the persistent revision journal under
`%LOCALAPPDATA%\\MonakaVR\\ViveUltimateTrackerBridge\\vut-service.revision.log`.

Before first use, copy `config/vut-service.example.conf` to `config/vut-service.conf` and replace
all example source/map values. The local config is gitignored. Close VIVE Hub / VIVE Streaming Hub
before opening the official dongle. Ctrl+C stops the service.

`reconnect_revision` controls coordinate-map revision behavior:

- omitted or `strict` (default): every successful backend start creates a new session, epoch,
  transport generation, and journal revision;
- `preserve` (experimental opt-in): an ordinary HID transport reconnect in the same
  `vut_service` process with the exact same configured source/device/map identity creates a new
  session, epoch, and transport generation but retains the current coordinate revision.

The preserve policy is not automatic Bridge approval and does not claim that VIVE always retains
its map. An exact configured device returning after an observed absence is treated as the narrow
tracker reconnect covered by this explicit opt-in; unknown addresses are still rejected. Sequence
exhaustion, ambiguous/uncertain recovery, service/process restart, config identity change, and any
path without sufficient continuity evidence advance the journal revision or fail closed.
Corrupt/torn journals always fail closed. Preserving a revision never rewrites the append-only
journal, and the next real advance receives the next monotonic value.

The current service configuration still keys mappings by the full RF address accepted by the
historical service path. Do not treat that address as persistent hardware identity; if it changes,
update the local config and restart for validation. This launcher does not fabricate or persist a
hardware identity.

Limited evidence for the opt-in is one 2026-09-21 HIL observation: after tracker power OFF/ON and
re-pair, `source=vive-local-1`, device `23:32:03:81:fe:ec`, and map `tracker-0-map` stayed the same;
strict mode advanced revision 55 to 56, while physical fixed-point pose comparison was broadly
consistent. This is evidence from that setup only, not a VIVE map-continuity guarantee. Map reset,
ambiguous RF recovery, other devices/firmware, and end-to-end approval behavior remain HIL gates.

Normal `vut_service` startup does **not** enter dongle pairing mode. It assumes the tracker is
already paired and only observes the existing relationship. Pairing/re-pairing is an explicit
diagnostic/setup action; restarting the service must not change pairing state.

## Explicit first pairing

Normal operation is for already-paired trackers only. For an intentional first-pair setup, stop
`vut_service` and VIVE Hub / VIVE Streaming Hub, then run the separate one-shot tool:

```powershell
.\build\Release\vut_pair.exe --slot all --timeout-seconds 60
```

`vut_pair` opens the dongle exclusively, enables PairDevice only after that explicit invocation,
waits for one `DevicePaired` event, prints its exact observed slot and six-byte RF address, and
disables PairDevice before closing. Timeout, Ctrl+C, HID error and uncertain command response take
the same bounded best-effort disable path. It never kills another process, unpairs an existing
tracker, edits `vut-service.conf`, approves a map or restarts the service. If the service or Hub
owns the HID interface, the tool exits with an ownership error.

The default `--slot all` uses the existing five-selector RF payload helper. `--slot 0` through
`--slot 4` request a one-hot selector. What selector a new tracker should use and how the dongle
assigns a resulting slot are vendor semantics that remain **UNKNOWN/HIL_REQUIRED**; do not infer
topology from the selector. Existing paired trackers are not deliberately unpaired, but the effect
of pairing mode on a populated multi-tracker dongle also remains a hardware gate. The printed RF
address is an observed admission/configuration key, not a persistent hardware ID.

The tool is software-built and mock-tested, but Hub-independent physical first pairing is
**NOT RUN**. Until that HIL is completed, “VIVE Hub is unnecessary for first pairing” is a goal,
not a validated production claim.

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

## Historical Task 3 common Observation boundary

The following describes the original C1 handoff. Current production uses the pinned
wire-v2 kit and the fail-closed mapping in the production integration guide above;
historical loss-orientation mappings are not current rotation-only support.

The fixed Task 1 kit is now consumed by `ViveObservationAdapter`, the bounded loopback
`ObservationPublisher`, and the Windows `vut_service` executable. Existing backend/probe targets
remain available without C1 (`-DVUT_BUILD_OBSERVATION=OFF`). Full instructions, exact mapping,
RF-counter limitations, test ledger and Task 4 handoff: [Task 3](docs/task3/implementation.md).

Implemented and mock/Windows-build verified does not mean hardware verified: no Hub-free dongle,
tracker, map, 1 m translation or quaternion-axis hardware checks were run for Task 3. Battery and
all derivatives remain null. `vut-native-v1` requires a verified upper-layer conversion.
