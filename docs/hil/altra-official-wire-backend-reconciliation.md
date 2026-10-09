# Altra official wire / Backend reconciliation

Offline reconciliation of the saved official B Pair/reconnect captures at
`d788dcaf17192312fed8434fee6172a9ebfaf0c0`, against production comparison base
`a594e4377ef2b5cca04e915187b0c12e59064839`. No hardware action, new capture,
runtime manipulation, device process, vendor transmission or production edit
was performed. This report supersedes tentative command interpretations in
the earlier acquisition report; its original capture history remains intact.

Source of Truth: the user-supplied Wire Backend Reconciliation instruction,
SHA256 `f11615083be2c1d7e9d60e76de0e095eeab75fe5d80d5be1fc79fac75683d47f`.
The active task permits research-only offline changes. It does not authorize
future HIL or any command proposed below. The coordinated MonakaProtocol v2
contract is unchanged; slot/RF remains diagnostic addressing data.

## Conclusions

| Boundary | Classification | Evidence and limit |
|---|---|---|
| PairingController operation lifecycle | **acceptable**, with exchange-validation follow-up | CODE: explicit enable, bounded wait, generation/selector filtering, disable and cleanup. Association success does not promise Connected/Ready or tracking. Official disable is outside the saved capture coverage. |
| Post-pair protocol implementation | **needs correction** for an official-aligned profile | WIRE: info burst completes before init; kind 03, actual B slot, FW2/ATM21. CODE: configure immediately before event publication when its default-on flag is enabled; kind 04, host ANI0/ATM20, later NAZZ→FW1. Causality and safe replacement are unproven. |
| Reconnect recovery | **needs correction / end-to-end unverified for B** | WIRE: two identical targeted behavior-2 commands around OFF/disconnect, query, later normal reconnection without PairDevice/PWS. CODE: DeviceUnpaired→slot-local behavior-1/PWS-off. Do not substitute behavior 2 automatically. |
| Identity | **Class B candidate** | Fresh NADS and NASS in both captures equal HIL-05 B byte-exact. No RF/slot-change, duplicate-device or explicit unpair/re-pair identity proof. |

Behavior 2 is a **Strong candidate for RestartRf**, not Verified semantic.
Behavior 6 is **Unknown / Historical label only**: the old pairing-clear
hypothesis is unresolved. Both Pair behavior-6 selectors exclude SLOT-B and
select RECEIVER-OTHER. B's later successful reconnect therefore does not
disprove a clear operation on that other position. It also does not establish
behavior 6 as Unpair. Neither behavior is safe to implement as an automatic
production command now.

## Evidence method and provenance

Classes are separate axes: **WIRE** is an observed official USB transaction;
**CODE** an implementation; **API** documented official API; **STATIC** a
binary/string observation; **HIL-HIST** a saved historical HIL result; **RE**
a reverse engineering hypothesis; **INFERENCE** this comparison's conclusion.
Confidence in bytes is not confidence in meaning, necessity or safety.

Both original pcapng files were re-exported with read-only TShark `-r` and
the same CSV fields. Every exported row matched the existing CSV. Original
pcap/CSV/receipts/private summaries were hash-checked before and after analysis:
unchanged. Private absolute times, serials, RF, USB topology and raw payloads
remain private. The sequence JSON's new `official_wire_backend_reconciliation`
section contains frame references, relative times, anonymous relations,
source hashes and reviewed synthetic framing fixtures. Older sections describe
their own captures and are not retroactively rewritten.

| Capture | Packets | SetFeature requests | Host TX kind 03 | Other host requests |
|---|---:|---:|---:|---|
| Pair | 15,636 | 13 | 9 | one query, PairDevice-on, two behavior 6 |
| Reconnect | 57,232 | 11 | 8 | one query, two behavior 2 |

All 24 SetFeature reports are 64 bytes, USB `data_len` 72 includes the eight
setup bytes, HID report ID 0, requested length 64. Every SetFeature has a
correlated status-0 USB completion. That proves transfer completion only.
No tracker semantic ACK is inferred from it. Distinct identical commands are
kept as distinct transactions. Frame-relative timing starts at each capture's
first packet; these two clocks must not be concatenated as one continuous run.

The original sanitizer missed the `usb.data_fragment` field and did not decode
kind 03 or `1e 01`. The research sanitizer now supports these forms and rejects
conflicting byte fields. Existing private CSVs were never edited. GetFeature
response bytes are absent from those CSV payload fields despite `data_len=64`;
independent TShark JSON `frame_raw` extraction after the validated USBPcap header
was necessary to inspect the saved response. No new capture was started.

## Reconstructed official sequences (WIRE)

### Pair

| Frame | Relative s | Event |
|---:|---:|---|
| 33 / 34 | 133.659110 | query SetFeature / successful USB completion |
| 35 / 36 | after query | GetFeature / 64-byte response with RF-status structure |
| 37 | 133.665076 | behavior 0, flag 1, three selected positions including SLOT-B |
| 73 | 296.045396 | DevicePaired input for fresh B RF |
| 105 | 296.525408 | NANA |
| 109 / 113 | 296.557406 / 296.589412 | NADS / NASS |
| 119 / 123 | 296.621406 / 296.653408 | NASI / NAPI |
| 127 / 133 | 296.685413 / 296.717407 | NAV1 / NARI |
| 137 / 145 | 296.749409 / 296.813411 | NAGN / NAZZ |
| 147 | 296.821018 | ANI<SLOT-B> |
| 151 / 153 | 296.822402 / 296.825401 | ATW / ACF60 |
| 155 / 157 / 159 | 296.828404 / 296.831442 / 296.834598 | ATS<epoch> / Wc<country> / ACF50 |
| 161 | 296.838749 | behavior 6, flag 1, RECEIVER-OTHER only |
| 162 / 164 / 167 | 296.841555 / 296.845635 / 296.851427 | pAS<parameters> / FW2 / ATM21 |
| 171 | 296.907100 | second identical behavior 6, same RECEIVER-OTHER |
| 267 | 298.741451 | first pose, raw status 0x03 |

No PairDevice-off, PWS, host NA or FW1 was observed in this capture. The
completion-button action occurred in the uncaptured inter-capture gap; this
cannot establish the official disable/cleanup sequence. Operator receipt
times are acknowledgment/request bounds, not exact physical gesture times.
Keep the original receipt uncertainty: DevicePaired falls inside the bounded
gesture interval and the final LED acknowledgment was later. Do not use a
late acknowledgment to move DevicePaired or claim a precise gesture latency.

### Reconnect

| Frame | Relative s | Event |
|---:|---:|---|
| 11515 | 46.385946 | behavior 2, flag 1, SLOT-B only |
| 11517 | 46.388871 | DeviceUnpaired input for B |
| 11519 | 46.392974 | second byte-identical behavior 2, same SLOT-B |
| 11521–11524 | from 46.395474 | query SetFeature, completion, GetFeature, RF-status response |
| 11539 | 115.552331 | DevicePaired after normal B ON, no Pair operation |
| 11571 | 115.888336 | NANA |
| 11575 / 11581 | 115.920338 / 115.952344 | NADS / NASS |
| 11585 / 11591 | 115.984337 / 116.024340 | NASI / NAPI |
| 11597 / 11601 | 116.056339 / 116.088338 | NAV1 / NARI |
| 11605 / 11615 | 116.120342 / 116.184344 | NAGN / NAZZ |
| 11617 / 11619 | 116.184775 / 116.186356 | ANI<SLOT-B> / ATW |
| 11621 / 11625 / 11627 | 116.189357 / 116.192440 / 116.195367 | ACF60 / ATS<epoch> / Wc<country> |
| 11629 / 11631 / 11633 | 116.198959 / 116.201406 / 116.215115 | FW2 / ATM21 / pAS<parameters> |
| 11751 | 118.536383 | first pose after new DevicePaired, raw status 0x03 |

No PairDevice, behavior 1/PWS, behavior 6, ACF50, host NA or FW1 is present
in reconnect. The RF diagnostic vector changed later than DeviceUnpaired and
returned after DevicePaired; it is an opaque vector, not a documented SDK state.
The first behavior-2 request precedes the input disconnect, so a policy
triggered solely by that input cannot reproduce the complete official ordering.
OFF receipt/UI lag prevents claiming which internal runtime observation
triggered that first request or whether one/two requests are necessary.

Pair has 7,671 pose reports after association. Reconnect has 28,441 overall,
22,699 after the new association. Both post-pair streams have raw statuses
0x03/0x13 only (current CODE maps both to Lost), changing payloads and finite
position/quaternion values. Historical UI Connected-equivalent and official
normal reconnect were observed, with setup still required. Map Ready / valid
optical tracking was **not demonstrated**. FW2/ATM21→pose ordering does not
prove FW2/ATM21 creates valid tracking or completes map setup.

## Decode and semantic audit

### TX kind 03 vs 04

WIRE layout: `[18, declared-total, kind, destination[6], 00, 01, ASCII-length,
ASCII, zero-padding]`. All 17 official ASCII sends use kind 03, all six
destination bytes equal fresh B's incoming RF bytes, control bytes are 00/01,
and declared total equals 12 plus ASCII length. ANI's digit equals B's current
slot under production's second-RF-byte-low-nibble rule. These are equality
checks; raw values and numeric SLOT-B are private.

CODE: production `buildAckTxPayload` uses kind 04 with the same six destination
bytes, controls and length position. Only the kind differs for a common
ASCII command. Thus an explicit kind-03 builder/test is justified as an
offline implementation candidate; automatically switching all production
traffic still needs HIL. No official kind-04 send was observed here. No claim
is made about other runtime operations, clients or multiple trackers.

RE: the original enum names kinds 03/04 as full/partial MAC. Mobile comments
describe six-byte vs first-two-byte matching, but its actual sender writes 04.
The single-destination trace establishes bytes, not matching semantics. The
original kind-03 comment's byte-8 value does not match the observed payload
offset 8 of 01 under this framing. Its size constraints are not a current
vendor specification. Multi-tracker collision/isolation tests remain required.

### RF behavior 2 / 6 and host query

WIRE behavior layout: `[1d,09,behavior,flag,selector[5],zero-padding]`, flag 1
and binary selector bytes in these observations. CODE uses the same seven-byte
RF payload shape. Names/boolean interpretation do not prove dispatch semantics
for every position or value. SLOT-B aliases rely on current CODE's address rule.

Behavior 2: official targeted OFF-side traffic, old RE naming, and historical
RF restart experiments support **Strong candidate**. No isolated experiment
demonstrates the exact causal effect, persistence impact, flag meaning or
repeat requirement. HIL-HIST includes cold-start success but clean runtime
cold-payload reuse failure requiring dongle re-enumeration. A later contaminated
success had multiple controllers and is not a clean backend PASS. Targeted
official traffic does not erase this failure history.

Behavior 6: two identical Pair transactions target RECEIVER-OTHER, not B.
The original pairing-clear comment remains **RE**, not CODE implementation of
a verified Unpair semantic. Pyvut copies the same enum blob, providing no
independent confirmation. B continuity neither confirms nor refutes effects
on another position. No DB effect, persistence change or clear success is
observed. Keep **Unknown**, destructive risk unresolved, no production sender.

Query: both saved SetFeature reports are literal `1e 01` followed by 62 zero
bytes. Pair frames 33→34 and 35→36, reconnect 11521→11522 and 11523→11524
correlate through USB `request_in`; all completions succeed. GetFeature returns
64 bytes, command 1e, declared response length 29, structurally RF status under
current CODE. This supports an **RF-status query candidate**, not proof of
read-only semantics under every firmware. Byte 01 is an observed special
request form, not a valid generic declared length. Only one query per capture
is observed, not periodic polling. Production has passive status parsing and
lacks this normal active request path. Historical RE's length speculation must
not cause the valid observed query to be reported as vendor malformed.

### ASCII commands and remaining unknown SetFeature

The JSON preserves every host request with capture/frame/time, direction,
HID request/report length, kind, selector count/relation, destination relation,
ASCII label/length, nearest selected command/info/pose predecessor/successor and repeat count.
The tables above list all 24 requests; none was discarded as generic unknown.
All ASCII sends share kind 03 and B destination; RF/query requests have no
ASCII destination. Argument redactions intentionally retain no raw country,
ATS timestamp or pAS control values. Semantic uncertainty remains:

| Command | Pair / reconnect count; ASCII bytes | Decode/evidence | Meaning and applicability |
|---|---|---|---|
| ANI<slot> | 1 / 1; 4 | WIRE slot equality; CODE numeric ID setter; RE device-ID/Wi-Fi association | Verified digit relation in this run; not a persistent identity or proof of host/client roles. Current host ANI0 mismatches SLOT-B. |
| Wc<country> | 1 / 1; 4 | WIRE country-shaped argument after ATS; CODE country setter | RE Wi-Fi-country label; regulatory effect and valid-country prerequisites not independently verified. Current host Wc occurs earlier. |
| ATW | 1 / 1; 3 | WIRE exact ASCII, RE tentative acceleration enable | Acceleration semantics unverified; no normalization or capability change justified. |
| ACF60 / ACF50 | 2 / 1; 5 | WIRE Pair values 60 then 50; reconnect only 60; RE camera-FPS name | Numeric values verified; units, policy, second-send necessity unknown. |
| ATS<epoch> | 1 / 1; 13 | WIRE ten-digit argument; RE time-setting hypothesis | Clock side effects and required time source unresolved. Raw argument remains private. |
| pAS<parameters> | 1 / 1; 9 | WIRE shape `pAS_` + two uppercase letters + three digits; STATIC string presence | Unknown operation/arguments. Pair before FW2, reconnect after ATM21. No guessed expansion promoted. |
| FW2 | 1 / 1; 3 | WIRE exact ASCII; CODE Mobile explicit sender | FW operation meaning unresolved. Not proven firmware write, nor proven harmless. Not proven a universal replacement for FW1. |
| ATM21 | 1 / 1; 5 | WIRE exact ASCII; CODE Mobile host branch; RE mode-21 hypothesis | Mode meaning/map/host-client dependencies unresolved. Setup remained required and poses Lost. |

FW1 and ATM20/11 are present in production CODE and older external CODE, but
absent from these official captures. This is a profile/ordering difference,
not proof that FW1 or modes 20/11 are invalid on all firmware. Mobile differs
internally too: its Pair host branch uses FW2/mode21, client mode11 and map
operations, while its NAZZ handler still contains FW1. It is supporting CODE
correlation, not an official safe reference sequence.

## External and historical source correlation

All source files were read-only, hash-pinned, cached outside the repository,
and never executed. Full file hashes/git blobs/URLs are in the new JSON section.
No third-party implementation was copied into this repository.

| Source (pinned) | Evidence relevant to reconciliation | Limit |
|---|---|---|
| [original RE enums](https://github.com/shinyquagsire23/vive_ultimate_tracker_re/blob/11ae3cbfd9a331694c72890df7dced77248b4ba6/tracker_toybox/enums_horusd_dongle.py), `11ae3cbfd9a331694c72890df7dced77248b4ba6` | CODE enum labels and RE comments; ACK/time/country/mode hypotheses in companion files | Descriptive names/comments do not establish vendor semantics or safe prerequisites. |
| [nijkah/pyvut](https://github.com/nijkah/pyvut/tree/fcfcd33f4c1f16b0d84f5f741dc1319abdc7942a), `fcfcd33f4c1f16b0d84f5f741dc1319abdc7942a` | CODE inherited protocol/framing/init | Original/nijkah/h228 dongle enum content matches; not three independent semantic observations. |
| [h228zhou/pyvut](https://github.com/h228zhou/pyvut/tree/7b3a7ad16b0c1b0a62afe7bef743fdb8094a67d9), `7b3a7ad16b0c1b0a62afe7bef743fdb8094a67d9` | CODE adds status-triggered bring-up/NA relative to baseline | Cannot import runtime/NA side effects without separate permission and HIL. |
| [Mobile HidBridge](https://github.com/mgschwan/ViveUltimateTrackerMobile/blob/79e8d3fb71cb34187d369596aefe2aabc195c318/android/app/src/main/java/io/xrworkout/htctrackerdongle/HidBridge.java), `79e8d3fb71cb34187d369596aefe2aabc195c318` | CODE lines 1321–1341 host FW2/21, client FW2/11; 1437–1448 targeting comment and actual 04 sender | Experimental Android path; map operations and copied hypotheses; NOT RUN. |
| [HTC SDK header](https://github.com/ViveRoboticsSoftware/vive-ultimate-tracker-sdk/blob/8685112780734c2513c66d53b9eb104db47afcf9/include/ultimate_tracker.h), `8685112780734c2513c66d53b9eb104db47afcf9` | API separates Connected, Initialized, Ready; higher-level Restart/Unpair commands | API names do not map behavior 2/6 or FW2 to HID bytes. SDK not executed. |
| mscheibl/pyvut `fcfcd33f4c1f16b0d84f5f741dc1319abdc7942a`; HukoOo/pyvut `fe38eac54fdfe156f49de630c890fc7efbd509c7` | CODE additional fork references | No independent official semantic proof. |

The refreshed local Git audit covers 22 available heads/fetched origin refs,
26 distinct relevant tip blobs and 34 blobs across 16 reachable relevant change
commits. It includes `dev/vivehub-bypass`, auto-reconnect, debug-console,
runtime-restart-observability and SLAM diagnostic branches. Deleted/unavailable
remote history is not claimed audited. Scope, commits, blobs and allowlisted
symbol line matches are embedded in JSON. Historical reconnect documents at
`experiment/vive-auto-reconnect` commit
`24228627f535c3fec253451a6598ef1540e26be5` explicitly distinguish cold-start,
failed runtime reuse, contaminated success and a proposed slot-specific gate.

STATIC: installed `vive_tracker_driver.dll` SHA256
`49ed00b392b5b915aa71f3845d8b1b2d73f3192a7610d4781e8fb2b3fb910a09`,
`ViveTrackerServer.exe`
`f459ecc348e7459ae6f3a4f3212e66433de7b1834c9d80b1776d3694d3fbe088`,
`VS_PC_SDK.dll`
`9676846b577a33afa7afe3abee68bbe8df0825bcdfe16f19f8943509d24a35a9`
were inventoried read-only. The driver contains FW1/FW2, ATW/ACF/ATS/Wc/pAS
strings; the targeted scan found no RestartRf or ATM21 literal in these three
binaries. Presence/absence is not dispatch, ordering or safety evidence;
strings can be assembled dynamically. Earlier SDK binary inventory is
historical STATIC, not a newly executed SDK or firmware analysis.

## Production base and state-machine differences

Exact CODE blobs: backend `37b674f9b7ffae5dc3d4cd11775759c210bb49fa`,
pairing `3c3b103de15728414876a2041be186f1f06d4156`, protocol implementation
`efa5c890f6b4aaa3711721af51c57972732eb4ce`. The comparison is to the stated
production commit, not a branch tip with later research guard changes.

| Concern | Official WIRE | Production CODE | Passive research guard CODE |
|---|---|---|---|
| Pair operation | query→behavior0-on→DevicePaired; disable unobserved | explicit begin, observe one bounded association, disable/cleanup | feature exchanges and automatic controls suppressed |
| Initial bring-up | DevicePaired→complete N-burst→init | pollOnce calls configureOnPair before DevicePaired publication | suppressed |
| Initial ASCII | ANI(slot), ATW, ACF, ATS, Wc, optional second ACF/pAS | ARI1, ATM-1; host Wc, ATH1, AWH1, ANI0, ATM20; client ATH0, AWH0, ANI(slot), ATM11 | suppressed |
| Destination | all 17 sends kind03, six-byte B equality | kind04; six bytes still physically included | no TX |
| N-burst completion | NAZZ→init; later FW2/ATM21 | any text prefix NAZZ→FW1, no per-connection complete-burst gate | suppressed |
| Disconnect | behavior2 before and after input unpaired, query | erase state, emit DeviceUnpaired, PWS-off for slot | suppressed |
| Reconnection | new pair input→fresh burst→init | same immediate configure; recovery success unverified for B | receive-only cannot itself establish recovery |
| Pose/ready | changing Lost poses; setup required | parse/publish separate from association | receive/decode evidence only |

PairingController (`src/pairing.cpp`): operation generation, monotonic timeout,
selector and valid-slot filtering bound the result; late/stale events are
rejected. Finish/cancel/shutdown attempt bounded Pair-disable cleanup, and
CleanupFailed retains uncertainty rather than converting to Paired. Begin
does not imply Ready. This lifecycle is acceptable in software scope; retain
its explicit enable and cleanup design. The official selector vector is not
equivalent to the current all/single selector API, and official cleanup is
unobserved. Do not force a controller rewrite from post-info command differences.

Transport validation remains a P0 boundary candidate: `sendCommandDetailed`
uses command ID matching and response framing, not proven RF behavior/flag/
selector acknowledgment semantics. `sendAck` returns success for a built
payload and ignores the exchange outcome. USB success, GetFeature match,
tracker ACK, association, Connected and Ready must have distinct outcomes.
Any stronger acknowledgment rule requires observed response fixtures; never
fabricate a response echo or treat an uncertain disable as confirmed.

Official software-state candidate (INFERENCE): association→info collecting→
info complete→profile-gated initializing→pose observed, with Ready separate.
Epoch must include transport generation plus full transient RF/connection
instance; slot alone is insufficient. Clear this state on disconnect, reopen,
timeout or destination change. Production currently conflates association
with immediate initialization and has an unconditional late NAZZ reaction.
Duplicate/late bursts, missing categories and first pose before completion
must fail closed. This is a proposed model, not a newly verified vendor FSM.

## Safety table

“Candidate” means implement/test offline only unless the named HIL gate and
separate command permission are met. No row authorizes transmission now.

| Item | Observed meaning / confidence | Risk / outstanding prerequisite | Production decision |
|---|---|---|---|
| behavior 0 | WIRE explicit Pair-on; RE pair label | persistent association change; official disable absent | retain existing bounded explicit lifecycle; no startup/auto-pair |
| behavior 1 | CODE PWS; absent from these WIRE captures | RF/power state and reconnect effects not verified for B | do not call it official recovery; isolate legacy behavior in future experiment |
| behavior 2 | Strong RestartRf candidate | historical runtime failure; selector/repeat/persistence effects unresolved | **NO automatic production send** |
| behavior 6 | Unknown, pairing-clear RE unresolved | possible destructive pairing state effect on non-B position | **DO NOT SEND / no production builder exposure** |
| TX kind03 | WIRE exact official framing | full/partial dispatch semantics and multi-device isolation unverified | typed offline builder candidate; HIL before profile activation |
| TX kind04 | CODE existing framing | official use absent here; partial destination ambiguity | keep distinct from 03; no universal equivalence claim |
| query1e01 | RF-status query candidate | special request semantics, firmware coverage, cadence unknown | offline recognition now; opt-in query only after gate |
| FW1 | CODE NAZZ handler; absent WIRE here | unconditional/stale ACK behavior, FW purpose unknown | gate/remove unconditional reaction in proposed work; no blind FW2 replacement |
| FW2 | WIRE after info/init | effect/safety/preconditions unknown, Lost only afterward | **NO automatic production send now** |
| ATM20/11 | CODE legacy host/client profile | applicability and map policy unverified for current B | no global mode change from a single-B trace |
| ATM21 | WIRE current official single-B run | mode/map/role semantics and valid tracking unverified | **NO automatic production send now** |
| ANI / Wc | WIRE current-slot/country-shaped arguments | slot is addressing; country configuration may alter RF policy | explicit profile and correct transient destination; no identity inference |
| ATW / ACF / ATS / pAS | WIRE exact shape, semantics mixed RE/unknown | acceleration/camera/clock/RF side effects not isolated | do not replay unknown bundle |
| NA | absent host WIRE; RE resend-info candidate | can provoke an info burst or side effects, prior acquisition prohibition | **DO NOT SEND**, separate opt-in gate required |

## Priorities and production implementation work breakdown

P0: propagate real exchange failures; keep cleanup uncertainty; separate
association and readiness; bound info-burst state by connection epoch; replace
unconditional NAZZ side effects with a profile gate. P1: typed kind03/04 framing,
current-slot addressing, optional query decoding, explicit official-profile
post-info sequencing and separately gated reconnect policy. P2: multi-tracker
roles/map state, firmware profiles and identity promotion evidence. Priority
does not waive command-safety gates.

Next proposed implementation branch: `fix/altra-wire-exchange-evidence`,
starting from the then-reviewed production lineage (comparison here is a594).
Branch names below are proposals; none was created in this research task.

| Stage / proposed branch | Files and bounded scope | Expected bytes/state and software tests | HIL gate | Rollback |
|---|---|---|---|---|
| 0 / `fix/altra-wire-exchange-evidence` | backend command result types, pairing adapter/tests; propagate failed Set/Get and separate USB/response/semantic outcomes | no new command bytes; fake transport failures/truncated responses/wrong IDs/timeouts/stale generation; cleanup uncertainty retained | no hardware needed for software merge; later controlled Pair cleanup evidence | revert forward commit; no DB changes |
| 1 / `fix/altra-info-epoch-gate` | backend per-connection info collector and tests | association/info/initialization/pose/ready distinct; destination+generation keyed; no automatic new sends; duplicate, reordered, missing, late NAZZ and reconnect tests | receive-only fresh burst gate first | disable collector/profile, preserve diagnostics |
| 2 / `research/altra-official-tx-profile` | protocol typed kind builder + fixtures/tests; default-off experiment configuration | same framing, explicit 03 vs04; strip HIDAPI zero gives expected 64 bytes; ANI current slot; no behavior6 | isolated single-B TX targeting gate with exact approved command set | default profile off; legacy path isolated, stop on mismatch |
| 3 / same experimental lineage | post-info plan/state machine; FW and mode policy separate | no ARI/ATM-/ATH/AWH inferred requirement; wait complete current burst; expected observed init order; timeout/no-ACK abort; never replay pAS/behavior6 by bundle | unknown semantics review first; authorized single-B post-info HIL only, one causal change per run | stop experiment and forward revert; official recovery only after evidence saved |
| 4 / `research/altra-reconnect-policy` | RF/query primitives and reconnect policy tests, independent of PairingController | typed 1e01 special request; parse correlated response; targeted behavior2 only if later gate permits; bounded attempts, no auto-pair, no cold payload reuse | semantic/safety gate before any behavior2, prove selector and no pairing clear; no commands based solely on string labels | sender stays off; manual official recovery after failure evidence |
| 5 / same experimental lineage | single-B reconnect experiment tooling/report | OFF→normal ON→fresh DevicePaired→new info→profile init→pose; association/UI/Ready distinct | clean sole Bridge control owner, A OFF, no charging USB; compare exact private capture to official baseline | stop at first failure; no extra Pair/Unpair |
| 6 / `research/altra-multi-target-hil` | addressing/role/map policy tests, no shared protocol change | all six destination bytes, disjoint targets; stale RF reuse cannot hit another tracker; client/host profiles must be independently supported | two trackers, collisions/slot changes/re-pair/firmware checks under new authorization | profile off, no automatic reassignment or pairing clear |
| 7 / `feature/altra-service-wire-profile` | service-owned backend integration/config/tests | one HID owner; explicit operation IDs/cleanup and opt-in profile; stable identity still fail-closed | prior stages repeatably PASS and service HIL | feature off and forward revert; retain private evidence |

Stages 2–4 may implement **inert, fake-transport-tested candidates** before
HIL, but must not activate behavior2/6/FW2/ATM21 or unknown ASCII simply because
the wire contains them. A blocked semantic gate can stop advancement while
P0 state/error work proceeds. Changes to MonakaProtocol must independently use
an actual clean, generated, hash-pinned v2 kit; no wire schema is invented here.

## Next Bridge HIL and identity gates (NOT RUN)

1. Preflight: separately authorize the exact experimental command set; freeze
   branch/build/profile/hash and sole controller ownership; A OFF; B-only;
   existing official association retained. Use sanctioned USBPcap extcap capture
   first, verify traffic, log immediate private operation receipts and bounds.
2. Begin with passive B fresh-burst reacquisition and epoch tracking. No NA,
   Pair, behavior2/6 or unknown init bundle. Confirm destination and exact
   NADS/NASS; if no fresh burst, mark unobserved and stop that gate.
3. Only after semantics/safety approval, test the kind03/current-slot post-info
   profile independently from reconnect. Capture exact send/USB/GetFeature/
   incoming outcomes; all argument sources explicit. Treat Lost poses as Lost,
   not Ready. No mode/FW/clock/country/unknown sends outside the approved set.
4. Only after the behavior2 selector/persistence gate, perform a clean normal
   B OFF, receipt, at least ten seconds, normal ON, no Pair operation. Do not
   blindly reproduce two repeats. Require bounded fresh association/burst and
   appropriate profile readiness. First failure ends the run: no Unpair,
   Remove, DB edit, extra Pair, dongle replug or competing runtime before saving
   failure evidence. Any recovery action needs the separately specified plan.
5. Repeat with controlled owner/generation/reopen changes before claiming
   autonomous recovery. Multiple trackers and map Ready are separate gates.

Class A identity promotion requires fresh bound bytes across explicit
unpair/re-pair (newly authorized), RF/slot reassignment, tracker/dongle power
cycles and transport/runtime restarts, and at least two physical trackers with
duplicate/conflict checks. Require identity bytes independent of transport
generation and safe handling of reused RF/slot. Firmware/country/profile
coverage and, where relevant, another PC/dongle remain scoped evidence rather
than presumed universality. This task's unchanged B address cannot satisfy
those gates. No serial or RF enters shared stableIdentity/public protocol.

## Validation and final classification

Validation: **15/15 Python research tests PASS**, including all 24 anonymous
command frames; exact CSV re-export equality and original-evidence hash checks
PASS; private identifier/time/raw-command leakage scan PASS; production files
unchanged. C++ build and hardware/HIL checks were NOT RUN because this change
contains only offline Python tooling and evidence documents.

Research sanitizer and reconciliation tests cover kind03/04, behavior2/6,
query, FW2/ATM21, unknown framing preservation, malformed/truncated data,
conflicting dissector fields, redaction, ordering and repeated transfers.
Reviewed synthetic fixture tests validate all 24 command frames and sequence
differences; destination/selectors/slot/country/epoch/pAS arguments are
substituted. They verify exact framing, not untouched private command bytes
or a runnable sequence. No production/backend process was launched.

Final classification:

- Behavior 2: RestartRf Strong candidate; standalone safety unverified; no activation now.
- Behavior 6: Unknown / Historical label only; non-B receiver targeted; no activation now.
- Official TX kind: 03 in both; current kind: 04. Required candidate: explicit typed 03 framing and targeting gate, not global unreviewed switch.
- Official post-pair: complete N-burst then slot-directed init/FW2/ATM21. Current: immediate role/host/mode20 or11 then NAZZ→FW1. Required: per-epoch post-info profile and independent semantics/HIL gates.
- FW1: current CODE, absent this WIRE. FW2: observed, semantics unknown. ATM20/11: legacy CODE, absent. ATM21: observed, map/role/valid-tracking meaning unverified.
- PairingController lifecycle: acceptable software operation boundary. Post-pair: needs correction for official profile. Reconnect: needs correction / B end-to-end unverified.
- NADS/NASS: both captures byte-exact HIL-05 B; remain Class B.
- P0: error/cleanup evidence and bounded info state. P1: explicit addressing/profile/query/reconnect candidates. Unknowns and no-send decisions remain in the safety table.
- Next branch: `fix/altra-wire-exchange-evidence`. Next HIL: passive single-B fresh burst first, then separately gated post-info and reconnect experiments.

Research-only outputs are committed to `research/altra-stable-identity-hil`.
No integration merge or production implementation is part of this result.
