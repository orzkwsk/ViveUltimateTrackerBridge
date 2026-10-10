# Altra single-B active reconnect HIL

Source of Truth: operator-supplied
`MonakaVR_Altra_B_Single_Active_Reconnect_HIL_Codex指示書.md` and the request's
independent per-command approval gates. Software-qualified base:
`b75e007ee72868797c51aa756322140f09c6ee6a`.
Branch: `hil/altra-active-reconnect-b-single`. No production/integration merge.

## Research boundary

`vut_active_reconnect_hil` is enabled only by the default-OFF
`VUT_BUILD_ACTIVE_RECONNECT_HIL` CMake option. It is excluded from release
packaging. Production sources, controllers, shared schema and APIs are unchanged;
production primitive/runner reachability remains zero. The prior primitive
reachability audit now explicitly permits only the isolated research session.

The runner opens exactly one uniquely enumerated dongle interface exclusively,
without a shared-open probe, fallback or reopen. Its default is passive
observation. Current RF status words are opaque diagnostics, never B identity.
Only a current incoming device address observed after B ON was requested can
become a candidate. Human confirmation of B-only, A OFF and existing registration,
a ten-second B ON baseline and fresh candidate evidence are required to bind.
Historical RF, historical slot and discovery order cannot authorize a send.

Each B2-1/B2-2/Q1E gate requires its explicit operation flag, exact current
slot/full RF destination, transport generation, observation/connection epoch,
run ID and independent one-shot nonce. A token does not replace human approval.
The coordinator must record the user's independent approval before writing that
gate request. Current capture proof also expires. Each gate is consumed before
durable intent and before a single SetFeature call. Only the separately approved
Q1E gate performs one GetFeature. Accepted API writes are not semantic ACKs.

Persistent process/trial/gate journals use exclusive CREATE_NEW and durable
flushes. Failed or throwing writes, malformed query framing, uncertain mapping,
slot/RF mutation, unexpected tracker/event storms and transport loss latch STOP.
No retry, Pair/Unpair, PWS, NA, FW/ATM, initialization or recovery sender exists.
A known single DeviceUnpaired after B2-1 is observed without erasing or rebinding
the target; repeated/unexpected Unpaired stops the trial. Each post-send passive
window starts after the API returns and lasts at least ten seconds before a next
gate can become eligible. Natural pose reception stops active escalation.
Fresh complete N-burst plus pose stops escalation immediately; Lost pose arrival
does not establish Ready. Vendor semantics and safety remain HIL_UNVERIFIED.

Raw reports, RF/address/slot, nonces, operator receipts, requests/responses and
capture evidence belong only in the private run directory. Terminal observation
holds ownership while capture is finalized/audited, then a normal shutdown signal
closes the runner. Operator/cleanup waits are bounded; no automatic second trial.

## Software gate

Windows x64 Release configure/build: PASS. Full CTest: 24/24 PASS.
Research runner SHA256:
`a5523f26e18a3fffd8f3d6c97de92122ba1abbc7dc609f8c7de7c353b2ead720`.
Release tooling and optimized tooling:
13/13 PASS each. Explicit checks cover ObserveOnly no-send, missing approval,
wrong slot/RF/epoch/run, duplicate sends, independent tokens, minimum windows,
capture freshness, mutation, write failure/exception and audit failure before
write, malformed query response and stop-after-reconnect. No test opens hardware.
Production reachability and no-retry static audits PASS normally and with
`python -O`; existing protocol/Backend/ObserveOnly regressions remain PASS.

## Hardware receipt

The preliminary observe-only invocation returned a generic open-failure label.
Subsequent zero-open SetupAPI metadata inspection proved that its `mi_00`-only
filter excluded the one target interface before CreateFile. This was a runner
selection defect; ownership contention was not demonstrated. The filter now
accepts both the qualified single-interface form and composite interface zero,
still requires a unique candidate, and retains detailed failure categories.
Synthetic single/composite/other-interface/product cases and full CTest PASS.
The operator performed the requested VIVE Hub normal open/close once. Any later
open success is confounded by this code correction and does not establish that
the lifecycle operation resolved an ownership conflict. Agent app operations: 0.
The preliminary passive capture was finalized/audited: zero vendor transmissions,
zero SetFeature, zero truncated frames; payload-bearing RF traffic was absent.

## Completed passive run

Run alias: `B-SINGLE-OBSERVE-01`, 2026-10-10. Frozen runner source:
`5cd171f7d30b61c602948158242fb78243375876`, with the SHA256 above.
The earlier selection-defect invocation was an infrastructure check, with no
active trial. Only the corrected runner proceeded to the physical B ON baseline.

Fresh operator preflight confirmed A OFF, B OFF, both charging USB disconnected,
dongle connected and VIVE Hub normally closed. The user performed VIVE Hub's
normal open/close once; the agent performed no GUI/process/service recovery.
SteamVR and Virtual Desktop remained running. Their protected process IDs and
creation times matched the pre-run inventory after cleanup. Background runtime
process presence does not identify a HID handle owner. The research runner's
exclusive open succeeded without shared-open fallback, with transport generation
1, and remained held through capture finalization and audit.

B OFF baseline: at least 68.568 seconds, seven RF status reports, no current
device candidate and zero vendor transmissions in the completed capture snapshot.
`b_on_requested` was durably acknowledged at runner elapsed 69.129 seconds before
the user was asked to turn on only B. The user's LED report was solid blue followed
by breathing. `b_on_confirmed` was acknowledged at elapsed 262.017 seconds.

The confirmed B ON passive window lasted 30.039 seconds. Three opaque RF status
reports arrived in that window; no RF status record transition, DevicePaired,
DeviceUnpaired, NANA..NAZZ, NADS, NASS or pose arrived. The runner observed 29 RF
status reports across the OFF baseline, operator wait and confirmed ON window.
Observation/connection epochs remained zero. No current device-address candidate
was available, so neither current slot nor full RF/address binding was established.
Opaque RF status and historical B identity were not used as a destination.

At elapsed 292.056 seconds the runner latched `CurrentBMappingUnavailable` and
stopped observation while retaining ownership. Natural reconnect was not
established. It did not request or send any active gate:

| Gate | Human active approval | Send count | Transport / result |
| --- | --- | --- | --- |
| B2-1 | None | 0 | NOT RUN; current binding unavailable |
| B2-2 | None | 0 | NOT RUN |
| Q1E | None | 0 | NOT RUN; response framing not evaluated |

Active hardware trial count: 0, against maximum 1. There was no persistent trial
intent, gate-attempt journal or send receipt. No retry, Pair/Unpair, NA, PWS,
additional power cycle, dongle replug or automatic recovery command was performed.
Observed event order was RF status only, with no association or information epoch.
No registration-loss or slot/RF mutation evidence was observed; mapping was never
established, so command side effects and mapping stability are not qualified.

## Capture and result

Capture was stopped normally first. Read-only inventory then confirmed zero
capture actors and one still-running research owner. Finalized raw pcap audit
completed before the normal shutdown request. The runner logged `NormalShutdown`;
post-shutdown inventory confirmed zero probe/capture actors.

The target VID/PID was confirmed from captured descriptors. The final capture
contained 98 target frames, including 31 incoming interrupt payload frames, zero
truncated frames, zero SetFeature frames, zero GetFeature frames and zero host
vendor transmissions. Thus automatic vendor action count was zero. Capture
includes traffic before the runner opened and during finalization, so its RF
payload count is distinct from the runner's 29 observed reports.

Raw capture SHA256:
`5d662d3101430955cff7e617e7df29bd63f276e0b3ddb69b529a9fa7d43abd7a`.
Raw pcap/CSV, identities, RF/slot, process inventories, original run ID, operator
receipts and journals remain private and ignored by Git. No fresh NADS/NASS was
acquired; byte-exact comparisons with HIL-05 B, official Pair B and official
reconnect B are NOT RUN. Identity stability was not evaluated, and current-session
identity acquisition was not established. Pose restoration was not established.

Classification: **HIL-F — current mapping unavailable; active effects unqualified**.
Software gate: **SOFTWARE_QUALIFICATION_PASS**. Safe reconnect sequence: **Unknown**.
Safe device-info acquisition: **NOT ESTABLISHED**. Vendor semantics and hardware
safety remain **HIL_UNVERIFIED**. No Behavior2/0x1e effect or ownership-cause
attribution follows from this run.

The run is closed. A future active gate first requires a separately designed,
current-session B mapping source; a prior slot/RF cannot authorize it. No further
trial or command is scheduled. Production changes: none. Integration/Stage 1/HIL
branch merges: none. Only this HIL branch carries the isolated runner, tests,
required CMake/test-boundary wiring and this sanitized report.
