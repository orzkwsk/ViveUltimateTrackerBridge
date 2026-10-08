# Altra / VIVE tracker setup: implementation and open boundaries

Base: `refactor/monaka-layer-separation`, commit
`e8ecd513391ab4f15af2941380631e7f05049bea`.
Work branch: `feature/altra-pairing-assignment-ux`.

**This is a setup foundation, not a completed config-free body-role workflow.**
Current Windows HID events contain an RF address but no verified hardware serial,
stable runtime UUID, or persistent individual identifier. The production setup tool
cannot safely assign and auto-restore a physical tracker. It displays the limitation
and blocks that operation. No RF address or slot is promoted to stable identity.
There is also no upper consumer body-role application API in this repository.

## Read-only architecture audit

| Question | Current code and behavior |
| --- | --- |
| Altra discovery entry | There is no separate Altra runtime adapter. `Backend::start` opens official dongle VID 0x0bb4 / PID 0x0350 through Windows SetupAPI/HID. `pollOnce` detects trackers from pair/unpair and pose packets. |
| Device identifier | `DeviceId::toString` emits the full lowercase six-byte RF address; `slotFromMac` derives an RF selector. Neither is verified persistent hardware identity. |
| Multiple trackers | Backend maps states/devices/buttons by vendor slot 0..4. Adapter streams and configured coordinate maps use full RF address. |
| Config | Line format: `source INSTALLATION_ID`, `map RF_ADDRESS COORDINATE_MAP_ID`, optional `reconnect_revision strict|preserve`. This is not body assignment. |
| Role enum / assignment | None in backend, service, or protocol Observation contract. Historical architecture delegates body roles to MonakaVR. |
| Reconnect | Service retries start after transport loss, creates a new session UUID/epoch/generation. Adapter unknown RF admission fails closed; absence then same-address pair requests restart. Strict advances coordinate revision; explicit preserve applies only to proved in-process same-map reconnect. |
| Stable identity retention | Address matching only. `src/backend.cpp` explicitly describes address change on re-pair. Automatic stable individual recognition is unavailable. |
| Identify | No LED, vibration, or haptic backend API. New setup module observes motion only. |
| Physical pairing | `beginPairing/endPairing` send real PairDevice enable/disable feature commands through `kCmdRequestRfChangeBehavior`. Feature response matching is not proof of successful pairing; `DevicePaired` is awaited. Tracker still needs physical pairing mode. |
| Existing pairing tool | `vut_pair` exclusive HID, selector/timeout, one pair event, bounded cleanup and exit codes. Prints admission-address/map suggestion; never edits config, assigns roles, or acquires serial. |
| GUI / interactive path | Previously probe, pairing, service and launcher CLI only. New `vut_setup` interactive console and launcher; no GUI added. |
| Headless | Existing `vut_service`, probe, pairing and diagnostic-only targets retained. Setup is optional and not launched by service. `--offline` and `--validate` never open HID. |
| Config compatibility | Existing service config parser and files unchanged. Versioned assignment sidecar is independent; no coordinate-map-to-body-role migration is valid. |
| Old branch | `dev/vivehub-bypass` is an ancestor. Selected backend/protocol comparison found no old-only behavior to import. Current passive start, cleanup and revised status policy must be preserved. |
| Existing tests | Decoder/status, fake pairing command/cleanup/exclusive transport, Observation MAC/slot/replay/loss semantics, session/revision reconnect, config/journal, publisher/loopback/cross-language. No existing body assignment tests. |

Official Linux SDK documentation exposes LED `Echo` Identify, but does not establish
an equivalent command for this repository's Windows HID transport. No guessed vendor
LED command was added. Reference:
https://github.com/ViveRoboticsSoftware/vive-ultimate-tracker-sdk

## Available workflow

On Windows run `Setup_Altra_Trackers.cmd`, or after building:

```powershell
.\build\assignment-ux\Release\vut_setup.exe .\assignments.json
```

The launcher builds setup and pairing tools and defaults its sidecar to
`%LOCALAPPDATA%/MonakaVR/ViveUltimateTrackerBridge/assignments.json`.
Use `scripts/setup_altra_trackers.ps1 -CMake PATH` when CMake is outside PATH.
Setup holds the dongle exclusively; stop the service and Hub yourself. No process
is killed, no physical pairing is entered automatically, and no pose is published.

1. If needed, exit setup and use `vut_pair`; put the tracker into pairing mode.
2. Start setup, power on trackers, and enter `refresh` for the event-driven list.
3. Enter `identify`, move one tracker, then `refresh` and `confirm RF_ADDRESS`
   during the five-second observation window. Re-run Identify if expired or ambiguous.
4. The list displays name, stable ID availability, serial/UUID availability,
   RF address, connection evidence, tracking state, role, last-seen/pose age,
   pose freshness, movement/sample totals, position/angular delta and warning.
5. `next` suggests the next connected unassigned device. `roles` lists the local
   setup vocabulary. `assign RF_ADDRESS ROLE [swap|replace|cancel]` requires both
   human-confirmed motion and verified stable identity; current live backend
   reports the missing identity rather than saving an RF address.
6. For already-saved identities, `reassign STABLE_ID ROLE [swap|replace|cancel]`,
   `unassign STABLE_ID`, `swap STABLE_ID STABLE_ID`, and `clear-all confirm` edit
   local assignments. `validate` checks local rules and explicitly reports the
   unresolved hardware/upper consumer boundary. `save` persists edits; quit with
   pending edits reports that they were discarded.

`refresh` is a passive event-list refresh, not an undocumented hardware enumeration
request. Connection means observed pair/pose presence; there is no guaranteed
liveness heartbeat. Stale pose is displayed separately and never treated as proof
of physical disconnect. Metadata does not refresh pose age. Transport/unpair marks
devices disconnected and retires live identity bindings. History is bounded to
256 RF addresses; restart setup to clear session history.

Motion Identify requires at least six distinct RF pose samples, three movement
increments, at least 100 ms span, and 250 ms freshness. Qualifying total motion is
0.12 m or 0.52 rad. Subthreshold noise is excluded; duplicate/old RF indices,
large reception gaps, invalid quaternions, localization/status jumps and large
single deltas do not create candidates. Only Ok position and verified post-loss
RecentlyLost orientation are usable. There is no numeric confidence field in
the current backend; status/numeric validity are the available evidence. Multiple
candidates remain ambiguous. Neither motion nor confirmation creates hardware identity.

## Assignment module and persistence

`vut::setup::Assignments` implements assign/reassign, unassign, swap, replace,
clear, role uniqueness and unknown-role rejection. `StableIdentity` admits explicit
hardware serial, stable runtime UUID, or persistent identifier provenance. Provider
integration should select serial first, stable UUID second, persistent identifier
third. No stable composite fingerprint can be derived from current packets, so no
fingerprint fallback was invented. `Devices::observeIdentity` is the injection
boundary for a future verified provider; **production does not call it**.

The `roles()` list is local setup vocabulary, not a substitute for an authoritative
MonakaVR enum. It must be reconciled when the actual consumer API is available.

`AssignmentStore` stores:

```json
{
  "schema_version": 1,
  "assignments": {
    "serial:EXAMPLE-FACTORY-SERIAL": { "role": "LeftFoot" }
  }
}
```

This example is illustrative, not discovered hardware. Do not seed production
assignments using unverified identifiers. Existing records survive absence and
reconnecting with verified matching identity retrieves the saved role independently
of RF selector, address, enumeration order and session. Replacing a physical device
with a different verified identity does not inherit the old assignment. The current
live path cannot perform this identity match automatically.

Parser rejects corrupt JSON, duplicate fields, invalid records/roles, duplicate roles,
unsupported schema versions, more than 256 records, excessive nesting and files over
1 MiB. Errors preserve the original; missing file is explicitly a new empty sidecar.
Save retains unknown top-level and retained-record fields. Explicit unassign/clear
removes those records. An exclusive `.lock`, optimistic original-byte check, durable
same-directory temporary write and atomic Windows replacement protect saves. Stale
writers fail rather than overwrite. A crashed writer may leave a lock/temp file;
remove only after verifying no setup writer remains. No automatic stale-lock deletion.
No prior assignment schema exists, so migration is unnecessary; older service config
is neither rewritten nor interpreted as a role mapping.

## Verification and unfinished integration

New tests cover assignment operations, duplicate/unknown rejection, identity kinds,
disconnected records, injected identity reconnect/order/slot/session changes,
simultaneous devices, replacement, TrackingLost/recovery, movement noise/replay/
freshness/ambiguity/rotation-only/invalid quaternion, save/load, unknown JSON fields,
invalid config, stale writers, lock rejection, and atomic-replacement failure.
Windows fake HID integration exercises pair/unpair/transport reconnect. The injected
serial tests prove module behavior, not production serial acquisition or hardware restoration.

To complete the requested workflow, supply/integrate:

- A verified per-tracker serial/stable ID discovery API or packet contract for the
  current dongle path, including stability across re-pair and replacement. Dongle
  HID serial alone identifies the receiver, not its individual trackers.
- The authoritative body role enum and consumer assignment/apply API. Sidecar roles
  currently do not change observations or downstream body slots.
- Service-safe setup/state exposure and immediate assignment application after the
  upper interface is known; current exclusive setup requires a stopped service.

MonakaProtocol packet schema/codec, MonakaBridge and all existing tracking modules
are unchanged. No body role was smuggled into device_id or coordinate_space. Full
config-free setup/auto-restore is **not complete**, and this branch is not presented
as merge-ready for that full requirement. Hardware tests are **NOT RUN**: one/multiple
trackers, physical Identify, role assignment, Bridge restart, power cycle, changed
enumeration, simultaneous reconnect, replacement, duplicate-role UX, TrackingLost/
recovery and config-free end-to-end completion.
