# VIVE production integration boundary

This cleanup builds forward from `fix/vive-pws-auto-reconnect` at
`a6dcb0bc79e89a9ce48c9e6dff3cd134dab21e9a`. It does not merge into
`refactor/monaka-layer-separation`. Software regressions establish the decisions
below; they do not establish hardware recovery or independent orientation quality.

## Production behavior

- Normal `Backend::start` opens HID without sending PairDevice or startup all-slot
  PWS commands. Existing tracking bring-up on an observed pair is retained.
- An observed unpair clears that slot's state and requests PWS-off only for that
  slot. The bounded RF payload builder remains unchanged. Its ability to build an
  all-slot diagnostic payload does not mean startup calls it.
- Default backend/service operation never sends persistent-loss `P63:0/4/5/6`
  queries. This is independent of how long loss lasts.
- Admission retains rejection reason counters. `lastRejectedRf` and
  `lastRejectedGapNs` describe only the latest duplicate/reorder/gap rejection;
  `lastUnknownDevice` describes only an unknown-device rejection. Successful
  admission does not overwrite these snapshots. `lastAcceptedRf` remains separate.
- Default service stderr reports session/revision, end-of-session totals and a
  brief rejection/unknown-device summary when rejection counts change (at most
  every five seconds). Full admission/config/ACK dumps require `--diagnostics`.
- `reconnect_revision` defaults to `strict`. The experimental `preserve` opt-in
  separates a new transport session (new UUID, epoch and generation) from the
  coordinate-map lifetime only for an ordinary reconnect in the same service
  process and exact configured source/device/map identity.

## Exact status normalization

There is no established mask of ignorable status bits. Only the two exact
additional values reported by prior HIL, `0x12` and `0x13`, are normalized.
This cleanup does not claim to have repeated those HIL observations.

| Raw byte | Decoded vendor status | Shared v2 observation |
|---|---|---|
| `0x00` | NoImagesYet | NONE / initializing |
| `0x01` | NotInitialized | NONE / initializing |
| `0x02`, `0x12` | Ok | FULL / tracked only with finite position and valid quaternion; otherwise NONE / lost |
| `0x03`, `0x13` | Lost | NONE / lost |
| `0x04` | RecentlyLost | NONE / lost |
| `0x05` | SystemNotReady | NONE / initializing |
| All other bytes, including `0xF2`, `0xA3`, `0x22`, `0x14` | Unknown | NONE / unknown |

The existing vendor `TrackerState` orientation flag for Lost/RecentlyLost is
retained for diagnostics. It is **not** evidence of a usable independent rotation
stream. At the shared boundary both components remain invalid/null and orientation
evidence remains `none`. No rotation-only capability or common fallback rule is
introduced. The quaternion check remains finite norm in `[0.5, 1.5]`.

## Explicit diagnostics

Passive service logging (does not enable SLAM queries):

```powershell
.\scripts\start_vive_backend.ps1 -Diagnostics
# Or: vut_service.exe CONFIG REVISION_JOURNAL [PORT] --diagnostics
```

The existing standalone `vut_probe` is a diagnostic tool: it prints pose, ACK and
diagnostic events. By default it also leaves persistent-loss queries disabled.
To deliberately enable those active HID requests for HIL:

```powershell
.\build\runtime\Release\vut_probe.exe --diagnostic-slam-query
```

Build the probe target first if only the launcher/service was built. Stop the
service/other dongle owners before using the probe. The option sends keys 0/4/5/6
once per slot after five seconds of observed Lost/RecentlyLost. Recovery, unknown
status, unpair or transport restart resets the diagnostic interval. These are
active requests, not a proven repair action; no meaning is inferred from returned
SLAM keys. `vut_service` deliberately rejects this option. `--help` on either tool
does not open HID. All-ACK collection is confined to diagnostic service logging
or the probe, and the old automatic five-second all-ACK capture window is gone.

## Launcher identity check

An existing service is reused only when exactly one process has a matching
absolute executable path, command-line executable/config/journal paths, UDP port
and diagnostic option. Unknown/uninspectable paths, relative arguments, differing
options or multiple processes fail closed without stopping any process. The check
uses Windows command-line parsing, including quoted paths with spaces. Option
matching is case-sensitive like the service; Windows path matching is not.

Success here confirms process identity only, not stream health, loaded binary
bytes, or the in-memory contents of a config later edited on disk. A matching
process is not rebuilt or restarted. Restart explicitly after changing config or
updating the executable. The launcher does not infer RF identity or hardware health.

## Identity, space and recovery limits

The six-byte RF address is **not a persistent hardware ID**. The current local
service config still keys mappings by the full RF address; unknown addresses fail
admission. If that address changes, update the local config and restart rather
than silently rebinding another tracker. Slot and RF packet index remain vendor
addressing/diagnostic data, not persistent identity or MTP sequence.

The pinned wire-v2 codec, source/session identity, independent observation/state
sequences, timestamp rules, explicit map identity/revision, `vut-native-v1` profile,
raw quaternion ordering and common-layer fallback semantics are unchanged.
In default `strict` mode every successful backend start reserves a new revision,
unchanged from the historical behavior. In opt-in `preserve` mode, only a plain
transport read failure/reopen or the exact configured device returning after an
observed absence may reuse the current revision. This VIVE-side classification is
the narrow tracker reconnect selected by the operator; it is not an automatic RF
rebind. Sequence exhaustion, ambiguous/uncertain recovery, configured source/device/map
change, process restart, and every other unproved continuity path reserve a new
revision or fail closed. The service has no trusted explicit map-reset signal;
an operator reset/reprovision therefore requires a service restart. Journal
corruption fails closed, reuse performs no journal write, and the next advance
appends the next value. Bridge coordinate-space approval remains exact and no
VIVE-specific policy is added to MonakaProtocol, MonakaBridge or MonakaVR.

The six-byte configured address remains an admission key, not a persistent
hardware identity. A changed address is rejected; neither policy automatically
rebinds it.

## Software evidence and hardware boundary

- `protocol`: exact statuses and every unconfirmed raw byte fail closed; existing
  pair/unpair, RF payload bounds and decoder regressions remain enabled.
- `observation`: all 256 raw statuses through both 37/39-byte parsers, admission
  and the fixed v2 codec; unknown statuses never have usable pose components;
  accepted frames preserve rejection snapshots. Existing lifetime/replay tests run.
- `runtime_policy`: default config never schedules SLAM requests; explicit
  diagnostic timing, reset/slot bounds, and service CLI opt-in boundaries.
- `launcher_process_guard`: mocked process inventory and real Windows command-line
  parser, including wrong executable/config/journal/options and inaccessible data.
  This test does not open HID or launch/stop any service.

Run `scripts/test_revision.ps1 Quick`, then `scripts/test_revision.ps1 Full`.
For a clean committed tree, run `python scripts/release_v2.py --java-home JDK17
--cmake CMAKE_EXE`; the release gate requires the new tests and binds its receipt
and archive to the actual source HEAD and pinned v2 kit. Recheck that directory
with `python -O scripts/release_v2.py --verify RELEASE_DIRECTORY`. Logs/receipts
live under ignored `build/revision-test-results`, `build/release-v2` and
`dist/release-v2`; a prior HEAD's receipt is not evidence for this cleanup.

Hardware/HIL validation performed as part of this code change: **NOT RUN**. This
includes actual HID traffic/ACK timing, default production absence of queries observed on hardware,
startup pairing effects, slot-local unpair/power-cycle recovery, persistent-loss
probe queries, native map/axis/unit checks, multiple trackers and physical
end-to-end Monaka/SteamVR behavior. SteamVR interactive: **NOT RUN**. Synthetic
software cases do not upgrade any of these claims to PASS.

Limited prior evidence supplied for policy design: on 2026-09-21, one HIL run
observed the same `vive-local-1` source, `23:32:03:81:fe:ec` device and
`tracker-0-map` after power OFF/ON and re-pair. Strict behavior advanced revision
55 to 56, and physical fixed-point pose comparison was broadly consistent. This
single observation does not prove a vendor guarantee. Preserve-mode transport
reconnect, map reset/reprovision, ambiguous RF recovery, multiple trackers,
firmware variation, and end-to-end Bridge behavior remain hardware gates.
