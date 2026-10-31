# ViveUltimateTrackerBridge repository instructions

This repository owns the VIVE Ultimate Tracker backend boundary for MonakaVR: official dongle -> Windows HID/SetupAPI -> vendor protocol decoding -> normalized backend events -> Monaka TrackerObservation / ObservationDeviceState publication.

## Current coordinated Architecture Revision

The older Task 3 document and its fixed C1/wire-1.0 handoff are historical baseline material for the delivered implementation. For the current coordinated revision, protocol/fallback/identity semantics are superseded by the Architecture Revision owned in `MonakaVR/MonakaProtocol` (`docs/architecture-revision.md` and `docs/C2.md`). Do not revert current v2 semantics merely to satisfy an older Task 3 paragraph.

Protocol-dependent migration work must use an **actual generated and hash-pinned wire-v2 kit** from a clean committed MonakaProtocol source tree. Until that artifact and manifest are supplied to this workspace, inspect/plan freely but stop at the protocol dependency boundary; do not fabricate hashes, infer v2 fields from v1, or hand-roll a substitute codec.

Where the current Architecture Revision does not conflict, the existing Task 3 hardware/backend constraints and historical evidence remain applicable.

## Codex task entry point

For backend history and non-conflicting VIVE-specific constraints, read `docs/codex/Task3_VIVE_Backend.md` and `docs/codex/task3/00_task1_handoff_status.md` completely before implementation. Treat their fixed-C1 sections as historical once the coordinated v2 handoff is active.

## Codex Cloud branch handling

The audited historical Task 3 base is `dev/vivehub-bypass` at `db7c6795055fc00054b43a64e3bbd658a3575932`.

For the current coordinated revision, continue from the prepared `refactor/monaka-layer-separation` lineage (or a managed `work` checkout derived from it). Codex Cloud may expose its writable checkout as `work`; this is allowed. Do not recreate/switch branches or rewrite history merely to match the GitHub branch name.

If ancestry does not derive from the prepared lineage, stop and report the mismatch instead of implementing against `main` or an unrelated branch.

## Repository-wide constraints

- Work only in this repository unless the active coordinated task explicitly states otherwise.
- Preserve the existing Windows HID/SetupAPI path, official dongle control, independent protocol decoder, backend event model, diagnostic CLI, and existing CMake targets.
- Preserve `Backend::{start,stop,pollOnce,setEventCallback,snapshot}` and the existing `parsePairEvent`, `parseIncomingEnvelope`, `parsePose`, feature-command and ACK behavior unless the active task requires a narrowly scoped verified fix.
- Do not add VIVE Hub startup/automation, Wi-Fi-only operation, tracker-count-limit bypass, SteamVR driver ownership, world/body calibration, Main/Fallback policy, Fusion/Fallback/IK, or body-role policy to this backend.
- Do not copy new third-party reverse-engineering source into this repository.
- Do not invent battery normalization, acceleration semantics, angular-velocity frame/units, coordinate transforms, liveness guarantees, or rotation-only capability that has not been verified. Unsupported loss semantics fail closed.
- MonakaProtocol owns the shared contract. Do not redefine the schema or hand-roll an incompatible codec.
- Treat slot/RF packet index as vendor diagnostic/addressing data, never as persistent identity or protocol sequence.
- Preserve unrelated existing work and uncommitted changes.
- Do not delete branches/files, force-push, rebase, amend, reset away work, or otherwise rewrite history.
- Keep implementation commits small and reviewable.
- Run applicable build/tests before reporting completion. Mark hardware and platform checks as PASS only when actually run.
- Never claim Hub-bypass hardware operation, multi-tracker behavior, loss/recovery, map behavior, or Wi-Fi/count-limit behavior unless it was actually validated.

If repository state, the active v2 kit, or observed hardware behavior conflicts with the current Architecture Revision, inspect and report the difference rather than silently adapting the shared contract.

## Portable product entry

- Normal startup uses script-relative fixed `Build/runtime` products and never builds, installs or registers them. Run the explicit repository build command to update that output. Do not use task worktrees, dated packages, fixed drive/user paths or external tool installation literals as runtime dependencies. Discover external tools through environment or PATH; preserve required pinned kits and frozen evidence.
