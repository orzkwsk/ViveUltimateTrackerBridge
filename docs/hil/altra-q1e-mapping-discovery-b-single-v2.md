# Altra Q1E-only single-B HIL v2

**Q1E-C — Q1E_RESPONSE_ONLY:** one independently approved, qualified query
returned the qualified response framing. No device candidate, connection epoch,
fresh info burst, pose or RF status transition was observed. The saved capture
and runner receipt agree on the exact request and response. This is a valid
response-only result for this single trial; vendor semantics and general safety
remain **HIL_UNVERIFIED**.

Source of Truth: the operator-supplied
`MonakaVR_Altra_Q1E_Only_HIL_v2_GUI_Extcap_Codex指示書.md`, the direct task request,
and the separate `ACTIVE HIL GATE Q1E-ONLY v2` approval. Earlier instructions to
stop capture before the runner are superseded by this task's runner-first order.

| Item | Result |
| --- | --- |
| Base | `3108fe2a1156f29a1c70d46b08c409c8d47ad64c` |
| Branch | `hil/altra-q1e-mapping-discovery-b-single-v2` |
| Frozen source | `c6e13de740b9d037559f7f9cabcebf3ca1a42f35` |
| Runner | `vut_q1e_mapping_hil`, Windows x64 Release, research-only |
| Run alias | `Q1E-V2-B-01`, 2026-10-10 JST |
| Active trials / Q1E send attempts / retries | 1 / 1 / 0 |

Frozen runner SHA256:
`461223cdc166fa3fd1ae7898eceab9c307528b23d56c8fc26b7df28b6a8023b3`.

## Software qualification

**SOFTWARE_QUALIFICATION_PASS.** Release configure/build and full CTest
**27/27 PASS**. Release tooling **13/13 PASS** normally and with `python -O`.
Static reachability and Q1E isolation also PASS normally and optimized.
Production primitive call sites remain **0**. The executable defaults to
ObserveOnly; its only feature write uses the unchanged offline-qualified
`buildObservedQuery1eReport()`. An accepted write permits exactly one GetFeature.
The persistent trial journal consumes the trial before the HID API call.

No-retry, duplicate-send rejection, missing/stale/wrong approval rejection,
current-generation binding, physical confirmation, recent capture proof,
targetless eligibility, natural reconnect suppression and minimum observation
windows PASS with fake transports. Behavior2 is inaccessible from this runner.
Research targets remain default OFF and excluded from release packaging.
No new primitive, hand-written packet or production send path was introduced.

The narrowly scoped runner change records and flushes final receive state,
closes HID and records `ExclusiveHidReleased` / `NormalShutdown` before GUI Stop.
CLI and isolation checks cover that order. The primitive, collector, backend,
service runtime and shared protocol contract are unchanged.

## Capture and physical gates

Capture path: **QUALIFIED_GUI_EXTCAP_PATH_REUSED** — Wireshark GUI with the
installed USBPcap extcap, current target-device-only selection. This run reused
the separately qualified path; it did not repeat the A/B/C qualification tests.

The user started a fresh elevated Wireshark session and confirmed the current
selector contained exactly one target device. All-device and newly-connected
device capture were disabled; descriptor injection was enabled. Before HID open,
the user confirmed capture active, target-only configuration, increasing packet
counter and no error. The user reconfirmed capture health at the active gate.
No saved pcap was required during live capture. Duration covered the full run.
No TShark live capture, direct USBPcapCMD/dumpcap launcher, custom FIFO or
PowerShell input injection was used. Computer-use was not connected.

Fresh operator preflight confirmed A OFF, B OFF, both charging USB disconnected,
dongle connected and VIVE Hub normally closed. Exclusive HID open succeeded on
the first attempt; no ownership recovery was needed. A stayed OFF. Only B was
normally powered ON after the recorded `b_on_requested` receipt; no pairing
operation was requested. The LED report was initial illumination, blue steady,
then blue breathing. `b_on_confirmed` was recorded before its timed baseline.

| Observation gate | Actual result |
| --- | --- |
| Exclusive owner | PASS; one exclusive open, no reopen |
| Transport generation | `GEN-1`, unchanged |
| B OFF baseline before ON request | 19.240 seconds, RF reports received |
| B ON confirmed baseline before query | 112.125 seconds |
| Natural reconnect before query | NO |
| Fresh NADS / NASS before query | NO / NO |
| Pose before query | NO |
| Current candidate / mapping before query | Unavailable / unavailable |
| Second tracker / registration-loss evidence | None observed |
| Vendor sends before approval | 0 |

Current mapping unavailability did not block the targetless query. The opaque
response never supplied a slot/RF/identity/readiness mapping to the runner.

## Individually approved query and passive result

The user explicitly approved **one Q1E only**, while confirming capture healthy,
at `ACTIVE HIL GATE Q1E-ONLY v2`. Approval was bound privately to the current
run and generation. A runner token alone was not treated as human approval.

| Item | Actual result |
| --- | --- |
| Command | Offline-qualified `ObservedQuery1e`, no slot/RF/address selector |
| Q1E send count | 1; accepted by transport |
| Feature response read count | 1 |
| Response classification | `QUALIFIED_MATCH` |
| Qualified framing | MATCH, including zero padding |
| Opaque record | 23 bytes, retained privately without interpretation |
| Saved official reconnect comparison | Framing MATCH; opaque record byte-exact MISMATCH |
| Observation after API return | 30.059 seconds, no additional command |
| RF status transition | 0 |
| DeviceUnpaired / DevicePaired | 0 / 0 |
| Fresh NANA..NAZZ | None observed |
| Fresh NADS / NASS | NO / NO |
| Pose | NO |
| Connection epoch | No active connection epoch established |
| Current candidate / mapping after query | Unavailable / unavailable |
| Transport loss, mutation, malformed response, retry | None observed |

Event order: one approved SetFeature, one qualified GetFeature response,
continued periodic RF status reports without an observed transition, then
`Q1E_RESPONSE_ONLY` at the end of the passive window. No Behavior2 or subsequent
active gate was attempted. No automatic recovery command was sent.

The record's disagreement with the saved official reconnect is solely an opaque
byte comparison. It does not establish any slot table, RF mapping, identity,
readiness or causal meaning.

## Finalization and saved-file audit

The terminal result stopped active logic. Final receive state was durably
journaled, followed by `ReceiveJournalFlushed`, HID close,
`ExclusiveHidReleased` and `NormalShutdown`. The executable exited normally
with code 0 and its process disappeared while GUI capture still ran. Only then
did the user perform Wireshark normal Stop and private pcapng Save, followed by
normal Wireshark exit. Final capture/runner actor inventory was zero.

| Offline `tshark -r` audit | Result |
| --- | --- |
| Header | Valid PCAPNG, magic `0x0a0d0d0a` |
| Complete blocks / parse | PASS / exit 0 |
| Packets / target packets | 158 / 158 |
| Current target VID/PID and private tuple | Confirmed, unique |
| Non-injected target packets | 152; descriptor injection alone was not counted |
| Truncated target packets | 0 |
| Raw Q1E SetFeature / GetFeature requests | 1 / 1, submissions and completions correlated |
| Request vs qualified fixture / receipt | Byte-exact MATCH / MATCH |
| Raw response vs runner receipt | Byte-exact MATCH |
| Automatic vendor actions outside approved Q1E | 0 |
| Behavior2 / PairDevice / RestartRf / NA | 0 / 0 / 0 / 0 |
| Additional vendor commands | 0 |

The CSV dissector did not populate a payload column for the GetFeature
completion despite declaring 64 data bytes. Its complete USBPcap raw packet
provided those bytes; header length, target, control stage and data length were
validated before exact comparison. This resolved an extraction limitation
without a hardware resend or capture retry. Standard USB HID SET_IDLE lifecycle
requests carried no vendor payload and were counted separately.

SteamVR, Virtual Desktop and VRChat received no lifecycle operation. Every
initially inventoried protected application process retained its original
process instance in the final inventory. No process kill, service control,
driver/addon change, replug or ownership attribution was performed.

## Interpretation and publication

Classification: **Q1E-C — qualified response only; no state transition**.
Observed wire behavior: the bounded qualified request/response path completed;
vendor semantic and general safety qualification remain **HIL_UNVERIFIED**.
Mapping discovery, device-info acquisition and reconnect utility were **not
observed in this single trial**. This result does not establish necessity or
sufficiency for reconnect.

NADS acquisition: NO; stability and historical comparison: NOT EVALUATED.
NASS acquisition: NO; stability and historical comparison: NOT EVALUATED.
No fresh values existed for comparison with HIL-05 B, official Pair B or
official reconnect B. Unexpected effects: none observed within the bounded run.

Raw pcapng, CSV, raw response, opaque record, identity-bearing data, operator
receipts, tokens, topology and process inventory are private only. Evidence
SHA256 values are retained in the private manifest; the public software hash
above identifies the frozen executable. No raw evidence or private identifiers
are included in this report.

Files changed: the isolated research runner's finalization, its CLI/isolation
tests and this sanitized report. Release/full CTest, tooling, reachability,
guard checks, leakage scan and `git diff --check` PASS. The branch publishes the
software commit and a separate report commit; this report's containing commit
identifies publication without embedding a self-referential hash.

Production changes: none. Production, Stage 1, prior HIL branch and integration
merges: none. Only the requested v2 HIL branch is pushed. The run is closed; no
retry, Behavior2 escalation or additional hardware gate is scheduled.
