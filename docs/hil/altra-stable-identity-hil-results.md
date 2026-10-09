# Altra identity HIL: results and acquisition gap

Research branch: `research/altra-stable-identity-hil`; instrumentation `cce2216`.
Status: **resumed on 2026-10-09 JST; passive HIL stopped at an acquisition gap**.
Original interruption reason: **user temporarily away (「一旦離席のため中断」)**.
The user subsequently requested continuation after charging/sleep. The initial capture
ended cleanly at generation 9; the resumed B-only capture ended cleanly at generation 11.
No research recorder remains running. The original absence and the current inability to
obtain fresh B identity ACKs are separate reasons for stopping.

This hardware trial is incomplete, not Class A approval. Raw serials and RF addresses
stay in local gitignored `hil-private/`. Aliases below preserve byte equality within
each namespace; RF aliases are diagnostic labels, not persistent identities.

## Initial trial conditions and physical confirmation

Windows x64, official HID dongle VID 0bb4 / PID 0350. VIVE Hub application was absent
when capture began. VIVE Streaming Service, ViveAgentService and SteamVR vrserver
were observed running; they were not authorized for termination. The recorder acquired
exclusive HID access. This does not establish a fully Hub-free runtime.

The human confirmed two labelled trackers were ready, then confirmed A ON / B OFF,
and subsequently A/B both ON. The new RF seen after each operation was annotated with
that physical label, bounded by capture run, transport generation and report interval.
No label was inferred from serial, RF index or slot.

HIL-01 run: `17914998458231804`. The process was terminated by the operator for
HIL-02 after flushed A/B observations; no clean capture_stop record exists for HIL-01.
The operation is recorded separately in private evidence. HIL-02 restarted the same
backend capture executable with both trackers left ON, run `17915002331153955`.

## Physical observations

| Physical tracker | Scenario | RF | Slot | NADS | NASS | Generation | Result |
|---|---|---|---|---|---|---|---|
| A | HIL-01 | RF-1 | 0 | NADS-1 | NASS-1 | 1 | Natural connect ACK observed |
| B | HIL-01 | RF-2 | 4 | NADS-2 | NASS-2 | 1 | Natural connect ACK observed |
| A | HIL-02 | Unbound | — | Not received | Not received | 1, new run | No identity ACK in 90 seconds |
| B | HIL-02 | Unbound | — | Not received | Not received | 1, new run | No identity ACK in 90 seconds |
| A | HIL-03 | RF-1 at disconnect; ON binding unavailable | 0 at disconnect | Not received | Not received | 1, new run | Simple OFF→ON confirmed; disconnect observed, no reconnect identity ACK |
| B | HIL-03 | RF-2 at disconnect; ON binding unavailable | 4 at disconnect | Not received | Not received | 1, new run | Individual power-cycle instruction followed; disconnect observed, no reconnect identity ACK |
| A | HIL-04 | Unbound | — | Not received | Not received | 1→8, new run | Transport reopened; no identity ACK |
| B | HIL-04 | Unbound | — | Not received | Not received | 1→8, new run | Transport reopened; no identity ACK |
| B | HIL-05 | RF-2 | 4 | NADS-2 | NASS-2 | 1, new run | B-first connect; unchanged values |
| A | HIL-05 | RF-1 | 0 | NADS-1 | NASS-1 | 1, new run | A-second connect; unchanged values |
| A | HIL-06-before review of HIL-05 | RF-1 | 0 | NADS-1 | NASS-1 | 1, HIL-05 run | Baseline references saved; no additional ACK |
| B | HIL-06-before review of HIL-05 | RF-2 | 4 | NADS-2 | NASS-2 | 1, HIL-05 run | Baseline references saved; no additional ACK |
| A | HIL-06-after | RF-3 | 3 | NADS-1 | NASS-1 | 9 | RF and slot changed; candidate bytes unchanged |
| B | HIL-06-after | Unbound | — | Not received | Not received | 9 | Blue/breathing with A ON; B-only retry interrupted |
| B | HIL-06-after, resumed run | Unbound | — | Not received | Not received | 1→11, new run | B-only ON after both-OFF/dongle reset; blue→breathing blue; no connect or ACK |
| A | HIL-07 review of HIL-05 | RF-1 | 0 | NADS-1 | NASS-1 | 1, HIL-05 run | A/B byte comparison; no additional ACK |
| B | HIL-07 review of HIL-05 | RF-2 | 4 | NADS-2 | NASS-2 | 1, HIL-05 run | Both namespaces differ from A |
| A/B | HIL-08 | — | — | — | — | — | Optional PC restart: not run |
| A/B | HIL-09 | — | — | — | — | — | Optional second dongle: not run |

NADS payloads were 12 bytes; NASS payloads were 13 bytes. No parser notes were
reported for these four ACKs. A/B differed independently in both namespaces; NADS
and NASS also differed within each tracker. NASI, NAPI and NAV1 were naturally
received for both trackers. Each identity ACK matched its exact saved raw envelope,
RF, slot, generation, receive timestamp and report reference in the offline comparison.

HIL-02 saved 31 non-pose raw reports but no pair or ACK events. Restart identity
stability could not be evaluated because there was no fresh candidate evidence;
automatic rediscovery did not occur in the observed 90-second window. Values from
HIL-01 were not carried into the new binding.

HIL-03 run `17915003355876539`: the human explicitly confirmed A underwent only
OFF→ON, with no re-pair. An `APF` ACK and decoded DeviceUnpaired event were received
from RF-1. No later DevicePaired or NADS/NASS ACK was observed at this checkpoint.
DeviceUnpaired is an input decoder event name; it is not proof that a pairing database
entry was removed. No power-cycle stability result can be inferred from missing ACKs.
The normal backend's slot-local RxPowerSaving(false) recovery is suppressed by the
passive command guard, as are all feature exchanges. Its effect on this reconnect has
not been isolated; do not attribute the failure to a removed pairing registration.
B's response to the individual power-cycle instruction described both trackers'
LEDs progressing to solid blue, then breathing blue. B also emitted APF followed by
DeviceUnpaired, with no subsequent DevicePaired or identity ACK. The capture ended
cleanly. Identity stability could not be evaluated in that isolated capture.
The [official LED description](https://www.vive.com/au/support/ultimate-tracker/category_howto/status-led.html)
labels breathing blue as standby; this does not establish pairing registration state.

HIL-04 run `17915008682883821`: with both trackers left ON, the human unplugged the
official dongle and reinserted it into the same USB port. HID read failure and six
not-found open failures were recorded, followed by successful exclusive reopen at
generation 8 (initial open was generation 1). Generation increments on every start
attempt, including failed attempts; it is not a count of physical reconnections.
No pair or identity ACK was received after reopen, and the human reported unchanged
LEDs. Transport recovery was observed; candidate stability was not evaluated. VIVE
Hub application remained absent on a read-only process recheck; ViveAgentService and
SteamVR vrserver remained running. The capture ended cleanly.

HIL-05 run `17915009835473812`: after HIL-04's dongle reconnect, the human switched
both trackers OFF, waited, powered B ON alone, and confirmed B's green LED; then powered
A ON while B remained ON and confirmed A's green LED. Fresh connect events and full
device-info ACK bursts were recorded for B then A. Each NADS and NASS value exactly
matched the corresponding HIL-01 value. Both RF addresses and slots were unchanged.
The comparison independently confirmed reversed pair-event order under bounded human
bindings, no raw/parser mismatch, no duplicate live candidate, and no changing value.
This confirms persistence through the combined intervening restart/power/dongle
sequence. It does not establish automatic rediscovery on restart, power cycle or
dongle reconnect alone, which failed to produce identity ACKs in HIL-02/03/04.
The HIL-05 capture ended cleanly.

HIL-07 replacement discrimination: the physically confirmed fresh A/B values from
HIL-05 were compared independently, and both NADS_A != NADS_B and NASS_A != NASS_B
were confirmed. This is a comparison of existing observations, not a separate device
operation or capture. The automated comparison tool conservatively still lists the
missing HIL-07 capture marker and does not consume this review annotation.

HIL-06 before-operation references preserve the original HIL-05 run, generation,
RF, slot, raw ACK/report references and timestamps in private evidence. Raw capture
scenario markers were not rewritten and no fresh ACK is claimed for this annotation.
Capture is closed for the human to inspect an official single-tracker unpair operation.
No repo unpair command exists; the [official pairing guide](https://www.vive.com/eu/support/ultimate-tracker/category_howto/pairing-ultimate-tracker-with-the-dongle.html) describes pairing,
but does not document that single-tracker removal UI. No guessed command is sent.

During HIL-06 preparation the human opened VIVE Hub and reported connection loss
at launch. The read-only Trackers.json recheck then showed one dongle with zero
cached trackers, changed from the two entries used for earlier correlation. Cache
contents do not prove the dongle's pairing registration was removed. A-only OFF→ON
was requested to distinguish reconnectability from registration loss. This is not
counted as an actual unpair/re-pair trial and no cache/database file was edited by
the research tools. The initial correlation remains an observation made before
Hub launch, not a claim about the later empty cache.
The human then confirmed the Hub list was absent and A's OFF→ON did not reconnect,
and recognized that re-pair was needed. Manual official Pair new was requested for
B only (A OFF), then A. This is vendor control outside the passive recorder, not
an undocumented feature command. Explicit single-tracker unpair remains unconfirmed.
The human confirmed B was connected/green after Pair new, then both A/B were
connected/green after A's Pair new. The exact former NASS values were again present
in the read-only Hub cache. With Hub still active, both trackers were switched OFF
and the human confirmed ten seconds had elapsed. The authorized Hub application
processes VHConsole and ViveHubServer were stopped after executable-path checks;
other services and SteamVR were not stopped. Exclusive passive HIL-06-after capture
opened successfully, run `17915016786807686`, before asking for A-only power-on.
The human reported A remained blue/breathing and B OFF; no reconnect ACK was received.
Both were then switched OFF, the dongle unplugged/replugged, and A alone powered ON.
The human confirmed A green / B OFF. The same capture reopened successfully at
generation 9 after seven failed not-found attempts. Its newly observed A RF differed
from the HIL-05 baseline and slot changed 0→3. NADS and NASS exactly matched the
pre-pair values. This is new raw ACK evidence in generation 9, not cache-inferred RF
or a previous generation's candidate binding. When B was powered ON while A remained
ON, the human reported B blue/breathing and no B connect or identity ACK was observed.
B-only power-on after another both-OFF/dongle-replug was requested. Simultaneous
post-pair A/B reconnection is not validated by this trial.
Before confirmation of that B-only operation, the user requested interruption for
absence. No B post-pair connect or NADS/NASS was recorded. HIL-06-after saved 137
non-pose raw reports and 16 ACK events, all identity ACKs from physically confirmed A.
The pending B operation must not be assumed completed. Current physical ON/OFF states
must be rechecked on continuation; only the historical confirmations above are known.
The comparer consumes raw scenario markers, not HIL-06-before/HIL-07 review annotations;
its missing-marker results are conservative and are not substitutes for the manual
provenance and incomplete-operation assessment in this report.

## Resumed B-only trial, 2026-10-09 JST

The user reported A green / B blue after charging/sleep, then confirmed both trackers
OFF for ten seconds and both charging USB cables removed. An initial exclusive-open
attempt failed. A both-OFF dongle unplug/replug also failed to resolve exclusive access.
Only VIVE Streaming Service was initially authorized for stopping. The ordinary service
stop lacked privileges; the normal Windows UAC elevation completed successfully, and
read-only inspection verified the service Stopped. Exclusive capture still failed.
A separate open-only diagnostic succeeded with shared access and failed with exclusive
access, Win32 error 32 (sharing violation); it performed no packet or feature I/O.
The actual HID-owning process was not identified. After the user's instruction to
continue the pending SteamVR-stop step, inspection found SteamVR already absent, so
this agent stopped no SteamVR process. Capture then acquired exclusive access. This
sequence does not isolate which process or state change had held the HID interface.

The new passive run `17915485064264249` used the research source at `d78dab2`.
VIVE Hub and SteamVR were absent and VIVE Streaming Service was Stopped. Both trackers
were still OFF. The human unplugged/replugged the official dongle during capture and
waited; transport loss, nine not-found start attempts and successful reopen were recorded,
advancing generation 1→11. Generation includes failed attempts and does not count
physical reconnects. The human was then asked to power B ON alone with A kept OFF,
and reported the pairing indicator followed by steady blue then breathing blue.
No connect event, disconnect event or ACK was recorded in the entire resumed run:
19 non-pose raw reports, zero NADS/NASS, clean capture_stop at generation 11.
No current RF/slot or physical identity binding could be established for B; saved RF,
Hub cache and serial values were not reused as fresh evidence. A was not powered ON
in this resumed run. The earlier A re-pair result remains historical evidence only.
Blue/breathing is the human's LED observation and does not prove registration loss.

The rebuilt passive instrumentation and identity_capture/identity_compare/identity_offline
CTest checks passed (3/3). Offline analysis of the six prior captures plus this new run
reported INSUFFICIENT_EVIDENCE with no fail-closed failures; the comparer still classified
NADS and NASS independently as B_HIL_REQUIRED with no parser/equality issues. No new
candidate bytes were available to test stability or mutation. No NA, ADS, ASS or other
feature exchange was sent; no production code or shared protocol change was made.

The passive HIL phase is stopped rather than repeating the same missing-ACK recovery.
The next phase is a read-only acquisition/queryability audit and a review of a verified
single-tracker unpair operation. It must identify a safe way to obtain fresh evidence
before more hardware trials; it does not authorize speculative queries. Both candidates
remain Class B and production integration remains not ready. At this checkpoint VIVE
Streaming Service is still Stopped; the last human state was A OFF and B blue/breathing.

## Exact namespace correlation

For both physically confirmed trackers:

| Candidate | Hub cache strTrackerSN | Historical Hub log SerialNumber |
|---|---|---|
| NADS | No exact match | Exact match |
| NASS | Exact match, one cache entry each | No exact match |

Sources were read only: local `ViveUltimateTracker/config/Trackers.json` and
`ViveHUB/Log/VHConsole*.txt`. Three distinct historical SerialNumber values were
observed across those logs. Matching used complete, case-sensitive strings; no
padding, substring, hashing, prefix or base conversion. Historical/cache correlation
does not prove current RF ownership. SDK Add(id,name) and a tracker USB descriptor
serial have not been correlated in this HIL.

## Candidate classification

| Candidate | A/B unique | Restart | Power cycle | Dongle reconnect | Slot/order | Re-pair | PC restart | Class |
|---|---|---|---|---|---|---|---|---|
| NADS | PASS, HIL-01/05/07 review | No fresh ACK on restart alone | Unchanged in later HIL-05; isolated rediscovery missing | Unchanged in later HIL-05; immediate ACK missing | Reversed order PASS; A slot 0→3 unchanged after re-pair | A: RF changed/value unchanged; B incomplete after resumed missing-ACK trial; explicit unpair unverified | Not run | B |
| NASS | PASS, HIL-01/05/07 review | No fresh ACK on restart alone | Unchanged in later HIL-05; isolated rediscovery missing | Unchanged in later HIL-05; immediate ACK missing | Reversed order PASS; A slot 0→3 unchanged after re-pair | A: RF changed/value unchanged; B incomplete after resumed missing-ACK trial; explicit unpair unverified | Not run | B |

NADS classification: **Class B**.
NASS classification: **Class B**.
Recommended persistent identity: **none verified**. NADS is the device-serial
candidate supported by exact Hub SerialNumber correlation; NASS is the separate
ship-serial candidate matching strTrackerSN. These are research candidates only.
Production integration readiness: **not ready**; no provider has been implemented.
Remaining HIL: B's fresh post-pair ACK/RF capture and slot-change assessment,
controlled explicit unpair/re-pair for both A/B, isolated restart/power/dongle
rediscovery resolution, and simultaneous A/B reconnect after re-pair. PC restart
and a second dongle remain optional.

## Continuation preparation

The user returned and explicitly requested continuation. The resumed B-only trial above
ended without fresh ACKs; no background retry, scheduled task, pending device query or
automatic vendor operation remains. The steps below preserve the original continuation
plan; repeating them is deferred until the acquisition gap has a verified resolution.

1. Read this report and the passive procedure in `altra-stable-identity-hil.md`.
   Keep branch `research/altra-stable-identity-hil`; do not merge into the base branch.
2. Read local `hil-private/HIL-06-status.json`, `bindings.json`,
   `HIL-06-before-references.json`, `HIL-06-Hub-correlation.json`, and the immutable
   raw captures. These files are not public attachments or committed evidence.
3. Reconfirm physical Tracker A/B labels, both OFF, official dongle present, and
   current vendor processes. VIVE Hub application was stopped; Streaming Service,
   ViveAgentService and SteamVR were not stopped. Do not assume their later state.
4. Start a **new** passive run before physical recovery/power operations:

   ```powershell
   .\build\stable-identity-hil\Release\vut_identity_capture.exe --scenario HIL-06-after --duration-seconds 900
   ```

   Record its actual new run ID. Do not reuse generation 9 or historical RF bindings.
   If initial exclusive open fails, resolve ownership/presence rather than downgrading.
5. With both OFF, unplug/replug the dongle, wait for successful transport open, then
   power **B alone** ON with A OFF. Confirm B's LED and observe its actual connect RF,
   slot, generation and fresh NADS/NASS. Cache index 2 is only a prior observation;
   it cannot establish the new RF binding. Do not send NA/ADS/ASS or other queries.
6. Add a new bounded human-confirmed binding. Compare exact candidate bytes against
   B's original HIL-05 raw references from the before-review file, and compare actual
   old/new RF and slot. If B reconnects, power A ON and obtain fresh A ACK/binding in
   that same new generation before claiming simultaneous post-pair reconnect.
7. Record missing ACKs and LED states if recovery fails. Resolve explicit single-device
   unpair through a verified vendor operation; never infer it from an empty UI list or
   decoder DeviceUnpaired. Do not guess a command or edit the pairing database.
8. End the recorder using its new `.stop` file, update sanitized tables, rerun the offline
   comparison with all capture files, and commit/push only public sanitized evidence.

Private capture run IDs, in order: `17914998458231804` (HIL-01),
`17915002331153955` (HIL-02), `17915003355876539` (HIL-03),
`17915008682883821` (HIL-04), `17915009835473812` (HIL-05),
`17915016786807686` (HIL-06-after). Each corresponds to
`hil-private/<run-id>.ndjson`. Run IDs are evidence metadata, not hardware identifiers.

## Instrumentation validation

Windows Release build passed, including verification of the pinned v2 kit and
historical v1 provenance. Seventeen CTest tests passed across runs; the UDP loopback
test required an unsandboxed rerun after a sandbox timeout. Java 17 cross-language
validation was not available. The two identity tests were rerun after final tool edits;
the Python comparison test contains eight synthetic cases and is not hardware evidence.
Passive fake-transport checks verified zero feature writes and zero feature reads,
including pair configuration, NAZZ response, unpair recovery and restart paths.
No production StableIdentity, MonakaProtocol or MonakaBridge change was made.

## Subsequent offline research

The [offline research checkpoint](altra-stable-identity-offline-research.md) analyzes
these saved captures and public static sources, separates stability from acquisition,
and proposes a provider policy and minimum remaining HIL. It adds no hardware trial
and does not change this interrupted Class B assessment.
