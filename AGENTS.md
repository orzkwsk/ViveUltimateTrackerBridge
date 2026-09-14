# ViveUltimateTrackerBridge repository instructions

This repository owns the VIVE Ultimate Tracker backend boundary for MonakaVR: official dongle -> Windows HID/SetupAPI -> vendor protocol decoding -> normalized backend events -> Monaka C1 `TrackerObservation` / `ObservationDeviceState` publication.

## Codex task entry point

For the current MonakaVR layer-separation work, read `docs/codex/Task3_VIVE_Backend.md` completely before making implementation changes. Also read `docs/codex/task3/00_task1_handoff_status.md` before any protocol-dependent work.

## Codex Cloud branch handling

The audited Task 3 base is `dev/vivehub-bypass` at `db7c6795055fc00054b43a64e3bbd658a3575932`.

The prepared GitHub work branch is `refactor/monaka-layer-separation`. Codex Cloud may expose its writable checkout as an internal branch named `work` rather than the GitHub branch name selected as the environment base. This is allowed if the checkout HEAD descends from the prepared Task 3 branch/base. Do not fail solely because the local writable branch is named `work`, and do not recreate/switch branches inside a Codex-managed checkout when remote branches are not exposed.

If ancestry does not derive from the Task 3 prepared base, stop and report the mismatch instead of implementing against `main` or an unrelated branch.

## Repository-wide constraints

- Work only in this repository unless the Task 3 specification explicitly states otherwise.
- Preserve the existing Windows HID/SetupAPI path, official dongle control, independent protocol decoder, backend event model, diagnostic CLI, and existing CMake targets.
- Preserve `Backend::{start,stop,pollOnce,setEventCallback,snapshot}` and the existing `parsePairEvent`, `parseIncomingEnvelope`, `parsePose`, feature-command and ACK behavior unless Task 3 requires a narrowly scoped verified fix.
- Do not add VIVE Hub startup/automation, Wi-Fi-only operation, tracker-count-limit bypass, SteamVR driver ownership, world/body calibration, Fusion/Fallback/IK, or body-role policy to this backend.
- Do not copy new third-party reverse-engineering source into this repository.
- Do not invent battery normalization, acceleration semantics, angular-velocity frame/units, coordinate transforms, or liveness guarantees that have not been verified.
- C1 belongs to MonakaProtocol. Use the fixed Task 1 protocol kit; do not redefine the schema or hand-roll an incompatible codec.
- Treat slot/RF packet index as vendor diagnostic/addressing data, never as persistent identity or C1 sequence.
- Preserve unrelated existing work and uncommitted changes.
- Do not delete branches/files, force-push, rebase, amend, reset away work, or otherwise rewrite history.
- Keep implementation commits small and reviewable. The `halfToFloat()` subnormal correction must remain a separate small commit with regression fixtures.
- Run applicable build/tests before reporting completion. Mark hardware and platform checks as PASS only when actually run.
- Never claim Hub-bypass hardware operation, multi-tracker behavior, loss/recovery, map behavior, or Wi-Fi/count-limit behavior unless it was actually validated.

If repository state, the fixed protocol kit, or observed hardware behavior conflicts with the task specification, inspect and report the difference rather than silently adapting the shared contract.
