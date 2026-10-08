# Altra stable identity: passive HIL

Base: `a594e4377ef2b5cca04e915187b0c12e59064839`.
Research branch: `research/altra-stable-identity-hil`.
NADS and NASS remain Class B until physical HIL is reviewed. No provider is added.

## Build and passive boundary

```powershell
cmake -S . -B build/stable-identity-hil -A x64 -DVUT_BUILD_IDENTITY_HIL=ON -DBUILD_TESTING=ON
cmake --build build/stable-identity-hil --config Release
ctest --test-dir build/stable-identity-hil -C Release --output-on-failure
```

`VUT_BUILD_IDENTITY_HIL` defaults OFF. Existing service/probe settings stay unchanged.
The research executable explicitly enables `diagnosticPassiveCapture`, takes exclusive
HID ownership, disables tracking configuration and never invokes pairing. Backend's
central command boundary rejects all feature write/read exchanges in this mode,
including pair configuration, NAZZ -> FW1 and unpair recovery. Descriptor enumeration
and interrupt input reads still occur. There is no NA, ADS or ASS query.
The capture mode cannot simultaneously perform re-pair: perform pairing as a separate,
identified operation, then reopen passive capture. Capture-before-pair is not achievable
with two exclusive HID owners. Do not claim natural connect traffic was captured if it
was emitted while the recorder was closed.

## Capture

Attach physical labels Tracker A and Tracker B. Keep raw files local, in gitignored
`hil-private/`; do not upload these files to the public repository. Stop authorized Hub
processes and check for other HID owners before capture. Exclusive-open failures require
attention; do not silently downgrade to shared access. Check vendor processes again if a
transport reconnect occurs. A capture session must contain no competing control sender.

```powershell
.\build\stable-identity-hil\Release\vut_identity_capture.exe --scenario HIL-01 --duration-seconds 180
```

Capture also exits cleanly when `hil-private/<run-id>.stop` is created. The stop
file is host-local control of the recorder; it causes no device command.

Start the capture while both labelled trackers are off, then power A on alone, verify
the observed RF address belongs to A, then power B on and verify B separately. The
console reports RF, slot, namespace and length, never candidate values. NDJSON retains
the original report hex, raw ACK hex, candidate byte hex, byte-preserving U+00xx text,
transport generation and immutable receive timestamp. Non-pose reports are saved;
identity ACK events reference their exact raw report. NAV1 is matched before NAV, and
firmware evidence includes its own receive time/reference within the same generation.
Length and alphabet are observations, not inferred vendor restrictions. Unknown N ACKs
use N_OTHER and are never identity candidates.

Physical labels default to null. After hand confirmation, add bounded annotations in
`hil-private/bindings.json`. This does not rewrite raw observations or infer labels from
slot or candidate serial. Example (replace placeholders using actual capture evidence):

```json
{"bindings":[{
  "capture_run_id":"ACTUAL_RUN_ID",
  "scenario":"HIL-01",
  "transport_generation":1,
  "rf_address":"ACTUAL_CONFIRMED_RF",
  "first_raw_report_index":1,
  "last_raw_report_index":20,
  "physical_tracker_label":"Tracker A",
  "physical_confirmation":true
}]}
```

Each interval must correspond to human-verified ownership. Stop the interval at
unpair, slot replacement or uncertain ownership. New transport generation/run needs
fresh physical confirmation and ACK evidence. A saved binding cannot establish current
identity. Capture run ID is host-local evidence metadata, never tracker identity.

```powershell
python scripts/compare_identity_hil.py --bindings hil-private/bindings.json hil-private/ACTUAL_RUN_ID.ndjson
```

The comparison output aliases serials and RF addresses while preserving exact equality.
It checks missing evidence, raw/parser mismatch, empty/nonprintable candidates, changed
values/lengths on a given RF, concurrent duplicates, unconfirmed/overlapping bindings,
per-physical-tracker stability, A/B difference, and changed RF after re-pair. NADS and
NASS are evaluated independently; equality between them is neither assumed nor required.
The slot/order check requires an actual slot change or reversed pair-event order under
human-confirmed bindings. Merely naming a run HIL-05 cannot satisfy this check.
`A_ELIGIBLE_REVIEW` only means the recorded prerequisites need human review; it does
not authorize production integration. Synthetic tests always remain Class B.

## Scenario markers and review checklist

| Marker | Physical operation | Evidence required for each A/B |
|---|---|---|
| HIL-01 | A then B on | ACK discovery and distinct candidate values |
| HIL-02 | Exit/restart capture process, trackers unchanged | New run ID; unchanged candidate; rediscovery separately recorded |
| HIL-03 | Power cycle A, then B | Old/new ACK; same candidate; RF and slot comparison |
| HIL-04 | Unplug/replug dongle while capture runs | Transport loss/open and changed generation; fresh ACK and bindings |
| HIL-05 | B then A / slot reorder | Physical ownership unchanged; actual order/slot change documented |
| HIL-06-before | Before unpair/re-pair A, then B | Current RF and candidate |
| HIL-06-after | After separate explicit unpair/re-pair | Different RF and unchanged candidate; both trackers |
| HIL-07 | Replace A with B in same test position | Different candidate values; physical labels verified |
| HIL-08 | PC restart, optional | New capture and unchanged values |
| HIL-09 | Pair with a second dongle, optional | Same physical tracker; unchanged value |

For HIL-06, do not replace an actual unpair/re-pair test with a power-off/on event.
Current `DeviceUnpaired` input events alone do not prove a persistent pairing database
entry was removed. Never clear the entire pairing database to simulate this scenario.

If ACKs are missing, enlarge the window, try the physical power/reconnect scenarios,
and capture after an explicitly authorized pair/re-pair. Stability and queryability are
separate results. Do not start active queries: report a separate queryability phase.

## Correlation and final report

Compare original exact bytes independently with Hub strTrackerSN/SerialNumber, USB
descriptor serial and SDK Add(id,name) if available. No prefix addition, substring,
zero padding, hashing or base conversion. Cache data is corroboration, never fresh RF
binding. Output both requested tables:

| Physical tracker | Scenario | RF | Slot | NADS | NASS | Generation | Result |
|---|---|---|---|---|---|---|---|
| A/B | NOT RUN until observed | — | — | — | — | — | NOT RUN |

| Candidate | A/B unique | Restart | Power cycle | Dongle reconnect | Slot/order | Re-pair | PC restart | Class |
|---|---|---|---|---|---|---|---|---|
| NADS | NOT RUN | NOT RUN | NOT RUN | NOT RUN | NOT RUN | NOT RUN | NOT RUN | B |
| NASS | NOT RUN | NOT RUN | NOT RUN | NOT RUN | NOT RUN | NOT RUN | NOT RUN | B |

Finish with NADS/NASS classification, recommended persistent identity, production
integration readiness, remaining HIL and identifier discovery gaps. Keep public reports
sanitized. Production provider and MonakaProtocol/MonakaBridge remain out of scope.
