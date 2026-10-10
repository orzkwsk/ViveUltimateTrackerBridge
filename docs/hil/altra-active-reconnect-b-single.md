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
`5a17407eaf3d56df1526b2db09489c0832fc6956ce4567b1b4822db95f5051d2`.
Release tooling and optimized tooling:
13/13 PASS each. Explicit checks cover ObserveOnly no-send, missing approval,
wrong slot/RF/epoch/run, duplicate sends, independent tokens, minimum windows,
capture freshness, mutation, write failure/exception and audit failure before
write, malformed query response and stop-after-reconnect. No test opens hardware.
Production reachability and no-retry static audits PASS normally and with
`python -O`; existing protocol/Backend/ObserveOnly regressions remain PASS.

## Hardware receipt

Hardware access/send: NOT RUN. Gate approvals: none. Send counts: all zero.
Fresh physical preflight, passive B OFF/B ON baselines, current binding and
individual active approvals are pending. No HIL classification or reconnect
effect is claimed before a new run is observed. Raw evidence remains private.
