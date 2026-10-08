# Altra identity HIL: observed results

Research branch: `research/altra-stable-identity-hil`; instrumentation `cce2216`.
This is an ongoing hardware trial, not Class A approval. Raw serials and RF addresses
stay in local gitignored `hil-private/`. Aliases below preserve byte equality within
each namespace; RF aliases are diagnostic labels, not persistent identities.

## Conditions and physical confirmation

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
cleanly. Both trackers' power-cycle identity stability remains untested.
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
SteamVR vrserver remained running. The capture ended cleanly. HIL-05 is now observing
B-first power-on after both trackers are switched off.

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
| NADS | PASS, HIL-01 | No fresh ACK; stability untested | A/B: no reconnect ACK | Transport recovered; no identity ACK | Pending | Not run | Not run | B |
| NASS | PASS, HIL-01 | No fresh ACK; stability untested | A/B: no reconnect ACK | Transport recovered; no identity ACK | Pending | Not run | Not run | B |

NADS classification: **Class B**.
NASS classification: **Class B**.
Recommended persistent identity: **none verified**. NADS is the device-serial
candidate supported by exact Hub SerialNumber correlation; NASS is the separate
ship-serial candidate matching strTrackerSN. These are research candidates only.
Production integration readiness: **not ready**; no provider has been implemented.
Remaining HIL: individual power cycles, dongle reconnect with fresh generation
evidence, reversed connection order/slot test, actual unpair/re-pair with changed RF
for A and B, replacement discrimination, and restart discovery resolution. PC restart
and a second dongle remain optional.

## Instrumentation validation

Windows Release build passed, including verification of the pinned v2 kit and
historical v1 provenance. Seventeen CTest tests passed across runs; the UDP loopback
test required an unsandboxed rerun after a sandbox timeout. Java 17 cross-language
validation was not available. The two identity tests were rerun after final tool edits;
the Python comparison test contains eight synthetic cases and is not hardware evidence.
Passive fake-transport checks verified zero feature writes and zero feature reads,
including pair configuration, NAZZ response, unpair recovery and restart paths.
No production StableIdentity, MonakaProtocol or MonakaBridge change was made.
