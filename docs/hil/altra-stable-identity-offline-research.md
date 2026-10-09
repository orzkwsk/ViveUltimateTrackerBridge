# Altra identity: offline research checkpoint (2026-10-09)

Completed against research checkpoint `4ebcb7ace84e700e2e24fec39b631fe36d4e1b94`,
descending from integration base `a594e4377ef2b5cca04e915187b0c12e59064839`.
The attached *MonakaVR Altra stable identity offline research* instruction is the
scope authority. No device was opened, queried, controlled, or captured in this task.
Production provider, packet behavior, MonakaProtocol and MonakaBridge are unchanged.
Historical HIL remains [the interrupted report](altra-stable-identity-hil-results.md).

## A. ACK acquisition conditions

| Question | Result |
|---|---|
| Natural send trigger | Fresh decoded connect event followed by the same nine N ACKs in five saved instances. This is observed association, not proof of the internal causal transition. |
| Known non-trigger | Capture/Bridge restart alone (HIL-02), isolated tracker power cycles (HIL-03), dongle reopen alone (HIL-04) did not yield fresh NADS/NASS in the recorded windows. These are failed observed attempts, not universal impossibility. |
| Possible request trigger | `NA` has a speculative RE annotation only. `FW1` is sent by normal backend after NAZZ, therefore cannot explain the earlier serial ACKs in these bursts. Passive capture suppresses it. |
| Unknown | Tracker CONNECTING→CONNECTED vs dongle handshake vs service request; passive suppression of RxPowerSaving recovery; guaranteed retry/query behavior, timing bounds, and firmware variation. |

Evidence categories: **A** tracker-side natural notification is consistent with passive
capture; **B** a request from another actor is not excluded (Streaming Service,
ViveAgentService and SteamVR were present historically); **C** connect-handshake
association is observed, but new pairing is not necessary in HIL-01/05; **D** pure
Hub cache replay cannot account for the saved HID ACK bytes, while earlier Hub actions
may have changed connection state; **E** the exact firmware trigger remains unknown.
No claim of fully Hub-free operation follows from exclusive HID capture.

Read-only audit covered current backend/decoder/capture, Git history (`-G` search),
`origin/dev/vivehub-bypass`, `origin/experiment/vive-auto-reconnect`,
`origin/experiment/vive-debug-console` and `origin/experiment/vive-test-harness`.
These refs keep the existing configure-on-pair path and NAZZ→FW1 response; none of
the searched backend/include/tool sources establishes an identity refresh request.
The command guard suppresses all feature exchanges in passive diagnostics. A decoded
DeviceUnpaired input is not evidence that pairing registration was deleted.

## B. NA: DO NOT SEND

Meaning: **UNKNOWN; hypothesized device-info resend**. Confidence in that interpretation
is low. Confidence that the available sources do not verify safe use is high.
No sender-side firmware switch, state machine, Android/native implementation or
request/response trace establishing its effects was available in the inspected sources.
No firmware extraction, daemon execution, binary download or active probe was performed.

Public RE source was inspected in memory at commit
`11ae3cbfd9a331694c72890df7dced77248b4ba6`; no third-party source was copied:

- [rf_hid_test.py](https://github.com/shinyquagsire23/vive_ultimate_tracker_re/blob/11ae3cbfd9a331694c72890df7dced77248b4ba6/rf_hid_test.py) defines `AKC_NA` with the uncertain comment “resends device info?”; no verified call/response.
- [ACK enums](https://github.com/shinyquagsire23/vive_ultimate_tracker_re/blob/11ae3cbfd9a331694c72890df7dced77248b4ba6/tracker_toybox/enums_horusd_ack.py) identify N-category device/ship serials and annotate receipt on connect.
- [misc enums](https://github.com/shinyquagsire23/vive_ultimate_tracker_re/blob/11ae3cbfd9a331694c72890df7dced77248b4ba6/tracker_toybox/enums_horusd_misc.py) name `BT_CMD_SEND_ACK`; an enum does not establish its dispatch conditions.
- [toybox sender/parser](https://github.com/shinyquagsire23/vive_ultimate_tracker_re/blob/11ae3cbfd9a331694c72890df7dced77248b4ba6/tracker_toybox/tracker_toybox.py#L794) handles N-prefix messages and responds to NAZZ with FW1. Its host ACK transport and connect setup do not implement a verified NA identity request.

## Device-info order and intervals

Generated [sanitized sequence evidence](altra-identity-offline-sequences.json) contains
only allowlisted fields. Five bursts share this exact observed order:

`pair → NANA → NADS → NASS → NASI → NAPI → NAV1 → NARI → NAGN → NAZZ`

| Scenario / physical tracker | Generation | RF alias / slot | Pair→NANA ms | NANA→NAZZ ms |
|---|---:|---|---:|---:|
| HIL-01 A | 1 | RF-1 / 0 | 344.0125 | 295.9949 |
| HIL-01 B | 1 | RF-2 / 4 | 320.0016 | 288.0051 |
| HIL-05 B | 1 | RF-2 / 4 | 359.9952 | 304.0266 |
| HIL-05 A | 1 | RF-1 / 0 | 319.9951 | 296.0161 |
| HIL-06-after A | 9 | RF-3 / 3 | 376.0098 | 288.0082 |

Most adjacent ACK gaps are approximately 31–33 ms, with occasional 40 ms gaps;
NAGN→NAZZ is approximately 64–72 ms. These are host receive intervals, not tracker
clock timing guarantees. Both identity namespaces occur once in every observed burst;
no missing member of this nine-message sequence was detected in these five examples.
This does not define mandatory vendor burst membership, nor guarantee future completeness.
The grouping tool uses a documented two-second observation gap, not a firmware delimiter.

**First pose order: UNKNOWN_NOT_RECORDED.** Existing capture deliberately drops pose
reports and has no pose callback. Gaps in raw-report index do not prove pose arrival.
Transport records lack monotonic receive times; generation changes can be located in
record order, but transport-open→burst latency cannot be computed. Generation increments
on start attempts, including failed opens, not only physical reconnections.
HIL-02/04 have no pair or ACK; HIL-03 has APF then disconnect inputs, no N burst.

## C. Stability and acquisition are separate

Identity stability means candidate bytes remain associated with the same physical
tracker across changed contexts. Acquisition means the backend can obtain fresh bytes
from the current live connection, independent of a saved RF/cache association.

| Candidate | Stability | Acquisition | Classification |
|---|---|---|---|
| NADS | A: same bytes in three run/generation contexts, two RFs and two slots. B: same bytes in two contexts, RF/slot change untested. A/B unequal. | Connect-associated bursts observed; restart/reconnect rediscovery unresolved. | Class B / HIL_REQUIRED |
| NASS | Same scope and limitations as NADS; separate namespace, separate values. | Same acquisition blocker. | Class B / HIL_REQUIRED |

Observed equality is not proof of lifetime persistence. A is stable in the observed
contexts but acquisition remains unresolved. B post-pair ACK is still missing.
Synthetic fixtures and offline tests cannot promote either candidate to Class A.

### Hub / SDK / USB correlation

Read existing `C:/ProgramData/HTC/ViveSoftware/ViveHUB/Log/VHConsole*.txt` with BOM-aware
UTF-16 decoding. October's log has ten SerialNumber occurrences and three distinct
values; both full NADS strings exactly match extracted historical values; neither NASS
does. September's main log has one SerialNumber occurrence matching neither A nor B;
its two rotated logs have none. No NADS/NASS/BT_CMD_SEND_ACK text was found there.
SerialNumber history contains no fresh RF binding or identifiable resend trigger.

Read-only `ViveUltimateTracker/config/Trackers.json` currently contains the two exact
NASS `strTrackerSN` matches, zero NADS matches. This differs from the temporary empty
cache documented during earlier Hub launch; no cache file was edited here. This cache
is corroboration only. Its indices, firmware and states cannot establish current RF
ownership or freshness; no timestamped N-burst correlation is established by these logs.

[HTC SDK header](https://github.com/ViveRoboticsSoftware/vive-ultimate-tracker-sdk/blob/8685112780734c2513c66d53b9eb104db47afcf9/include/ultimate_tracker.h#L125)
defines `Add(tracker_id, name)` but does not map name to either namespace.
[IPC reference](https://github.com/ViveRoboticsSoftware/vive-ultimate-tracker-sdk/blob/8685112780734c2513c66d53b9eb104db47afcf9/tracker_ipc_client_ref.py)
decodes an optional tracker-name payload. **SDK NADS/NASS correlation: UNKNOWN**;
string length is not matching evidence. No SDK server was started.

No saved, human-bound direct-USB tracker descriptor serial was found in this repository's
identity evidence. USB enrollment is a low-priority hypothesis only. Future full-byte
wireless NADS↔USB serial equality could support enrollment, but would still require a
fresh wireless association for the current session; dongle USB serial is not tracker ID.

## D. Proposed IdentityProvider (design only)

Prefer **Policy B: NADS primary / NASS cross-check**, after separate Class A review
and acquisition verification. Today every production result remains Unavailable.
No production code, schema field, codec or stable provider was added.

| Policy | Persistence / compatibility | Duplicate / firmware / partial burst | Migration |
|---|---|---|---|
| A: NADS only | Best current device-serial evidence; ignores ship serial. | Must reject duplicate/mutation; unavailable without fresh NADS. | Single namespace key; missing cross-check. |
| B: NADS + NASS check | Preserve primary key while retaining corroboration. | Disagreeing previously enrolled tuple is Ambiguous; partial NADS needs an explicit verified policy. | Can add cross-check without changing key; preferred proposal. |
| C: tuple | Stronger discrimination only if both persistence properties are verified. | Either changed/missing member prevents binding; firmware variation may split a device. | Adds composite namespace and migration complexity; no unverified concatenation. |
| D: NASS fallback | Ship-serial use not yet independently qualified. | Missing NADS must not silently select a second identity for one tracker. | Requires reviewed alias/enrollment migration; disabled initially. |

Conceptual evidence (internal only, not a proposed MonakaProtocol wire schema):

```text
IdentityEvidence {
    namespace, exactValueBytes, rfAddress, slot,
    captureOrServiceSession, transportGeneration, connectionEpoch,
    observedMonotonicNs, rawReportReference, source
}
IdentityProviderResult = Verified | Unavailable | Ambiguous | Stale
```

Freshness rules:

1. New service session or transport generation retires all RF bindings. Generation
   alone is insufficient across restarts: it resets per backend lifetime.
2. Require evidence received in the current session/generation/connection epoch,
   after connect, with proven raw provenance. Pair, disconnect, slot replacement,
   transport loss and uncertain ownership invalidate the previous association.
3. Never rebind a stored candidate to a current RF using saved RF or Hub cache.
   Keep historical candidate bytes for enrollment/mutation checks only.
4. Define a monotonic acquisition deadline and late-evidence policy after HIL; do not
   invent a time-to-live from the observed 288–304 ms burst. A fresh receive time
   proves observation time, not firmware manufacture/persistence.
5. Duplicate namespace/value on two live RFs: Ambiguous, quarantine both, no publication
   as Verified. Mutation on a human/enrollment-confirmed tracker: explicit failure,
   quarantine until reviewed. Namespace equality is neither assumed nor required.
6. Both candidates: exact-byte cross-check against reviewed enrollment. NADS only:
   initially Unavailable until the partial-burst policy is verified. NASS only:
   Unavailable, no automatic fallback. Conflict/overlap: Ambiguous. Retired evidence:
   Stale. No fresh evidence: Unavailable. Incomplete burst does not prove disconnection.

Verified is reserved for a future reviewed provider with Class A evidence, never emitted
by this offline tool. Candidate and RF aliases are report-local labels, not identities.

Upstream Monaka responsibilities remain assignment persistence and body-target policy.
A future reviewed contract may need backend namespace `vive-ultimate`, serial namespace,
exact candidate value and assignment target owned upstream. These are conceptual
requirements, not fields added to MonakaProtocol. Any wire change needs its owner and
the actual generated/hash-pinned kit. This backend must not own body-role enums.

Future `vut_service` should own the one HID session, tracking, pairing lifecycle, registry,
fresh identity acquisition and local status/control interface. Direct-HID setup remains
bring-up. Setup UI should consume service state rather than competing for HID. Status
should expose unavailable/ambiguous/stale reasons separately from tracking availability;
no large IPC implementation is included here.

### Small existing-code audit

Provisional storage still has a legacy `role()` accessor and “role already assigned”
diagnostic. A later small rename to `label()` is worthwhile, but is unrelated to this
research and was not mixed into it.
`PairingController::finish` overwrites `result_.reason`; unresolved cleanup followed by
shutdown/cancel can replace the original reason with the shutdown reason. `cleanupError`
retains its first nonempty string at controller level, but Backend's bounded retry
exposes only its final lastError. A separate future fix should retain primary operation
failure and first cleanup attempt failure, with a fake-transport regression. No pairing
runtime behavior was changed in this task.

## E. Offline tooling and verification

Added `scripts/analyze_identity_offline.py`, `tests/identity_offline_tests.py`, CTest
registration and the sanitized JSON artifact. Input is saved NDJSON plus bounded,
human-confirmed bindings. Output aliases run IDs, RF and candidate bytes, whitelists
namespaces, omits ACK payloads/free text/USB/Hub identifiers, and provides cross-namespace
`equality_token` values. Exact bytes are canonicalized before comparison, never trimmed,
case-folded or converted to a guessed serial format.

The tool verifies ACK and pair/unpair raw envelopes and receive metadata, reports every
N ACK in sequence, groups bursts by run/generation/RF/connection, compares contexts,
detects mutation and live duplicates, retires associations at connection boundaries,
rejects malformed/reordered evidence and preserves missing-marker/binding uncertainty.
FAIL_CLOSED exits 2; insufficient evidence exits 0 to allow a research report, but
**never means PASS**. Bad NDJSON exits 2 without echoing private values or paths.

```powershell
$captures = Get-ChildItem -LiteralPath hil-private -Filter *.ndjson | Sort-Object Name
python scripts/analyze_identity_offline.py --bindings hil-private/bindings.json $captures.FullName
python tests/identity_offline_tests.py -v
```

Six immutable saved captures produced five bursts, zero analyzer failures and
`INSUFFICIENT_EVIDENCE`. Missing required raw scenario evidence remains reported for
HIL-02/03/04, HIL-06-before, HIL-07 and B HIL-06-after. Historical review annotations
for HIL-06-before/HIL-07 are not rewritten as fresh observations.

Validation on Windows x64 (Visual Studio 18 2026 / SDK 10.0.26100.0):

- `cmake --build build/stable-identity-hil --config Release`: **PASS**. CMake also
  verified pinned v2 artifact/manifest/content and historical v1 kit provenance.
- Initial `ctest --test-dir build/stable-identity-hil -C Release --output-on-failure
  --output-junit offline-results.xml`: 12/18 passed; six sandbox failures (temporary
  file permissions/atomic replacement and UDP loopback timeout).
- Unsandboxed `ctest --test-dir build/stable-identity-hil -C Release --rerun-failed
  --output-on-failure --output-junit offline-rerun-results.xml`: **6/6 PASS**.
  Thus all 18 distinct CTest tests passed across the two runs.
- New Python suite: **16/16 synthetic tests PASS** via final
  `ctest --test-dir build/stable-identity-hil -C Release -R '^identity_offline$'
  --output-on-failure --output-junit offline-final-results.xml`.
  The final suite includes the fail-closed slot-replacement regression. This rerun was
  needed after that final review change, without repeating unchanged tests.
- Actual-private-value scan of four new files: **PASS**, zero leaked complete serials,
  serial hex strings or RF addresses across 14 private representations. No raw NDJSON,
  USB/Hub serial or third-party source is included in commits.
- Java >=17 unavailable (installed runtime 1.8): cross-language test **NOT RUN**.
- Hardware checks for this task: **NOT RUN**. Tests establish offline behavior only.

## F. Minimum remaining physical HIL (deferred)

Do not perform these steps until the human explicitly resumes hardware HIL. The old
pending B instruction was cancelled on departure; no current device state is assumed.

1. Reconfirm physical labels, current state and exclusive ownership. In one **new**
   authorized passive run with both OFF, reopen the dongle and power B alone ON.
   If B connects, bind its fresh NADS/NASS and actual RF/slot with a new bounded human
   annotation. Compare to original HIL-05 B bytes. This single observation addresses
   B post-pair identity, RF and slot change; unchanged slot must be reported honestly.
2. Keep B ON; power A ON in the same run. Capture and independently bind fresh A ACK,
   confirming simultaneous post-pair A/B reconnect without a second replay/enrollment.
   If either lacks ACK, record absence/LED state and stop rather than retry indefinitely.
3. Separate confirmed vendor single-tracker unpair→re-pair only if needed for Class A
   review. Empty Hub list, APF and DeviceUnpaired do not prove explicit unpair. Do not
   guess a command or edit the database; pairing cannot overlap exclusive passive capture.
4. Acquisition remains a separate gate even if 1–3 succeed. First choose one isolated
   restart trial with trackers connected; capture fresh evidence in a new session, and
   separately record first-pose timing if required. If no identity arrives, stop and
   investigate verified recovery/request semantics. **NA remains DO NOT SEND**; its
   speculative annotation does not authorize a query experiment.

The next B-only trial can close several missing stability observations; it cannot by
itself prove a reliable startup acquisition strategy or production readiness. PC restart
and second dongle remain optional. Large setup/service changes are unnecessary for this
next trial. Pose timing would require a separately reviewed diagnostic capture change;
no capture was resumed or modified to wait for physical events here.

## G. Delivery boundary

Commit/push only on `research/altra-stable-identity-hil`. Keep attachment and private
evidence untracked/ignored. Do not merge into `refactor/monaka-layer-separation`.
Merge recommendation: keep as research until acquisition and Class A review are resolved;
the offline analyzer may be reviewed independently of production identity integration.
