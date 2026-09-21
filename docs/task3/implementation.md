# Task 3 implementation / Task 4 handoff

C1 status: fixed Task 1 candidate / wire 1.0 / master reconciliation pending.

Historical handoff only. Current wire-v2 production status/validity and diagnostic
defaults are documented in [production integration](../production-integration.md).
In particular, the C1 loss-orientation table below is not current v2 behavior.

## Ownership and compatibility

Official dongle HID/SetupAPI, VID/PID filtering, feature commands/ACK bring-up, independent
37/39-byte decoder, vendor diagnostic types and vut_probe remain in the backend.
Backend::{start,stop,pollOnce,setEventCallback,snapshot} remain source compatible. Defaulted
receipt time and generation fields are appended to BackendEvent; rebuild consumers (no binary
ABI promise). transportGeneration() is additive. Legacy snapshot retains slot-based semantics;
only timestamped receive events feed C1, never replayed snapshots.

ViveObservationAdapter maps into supplied C1 models. ObservationPublisher owns bounded loopback
publication. vut_service composes these. Generic mock consumer links only the fixed codec, with
no VIVE includes. No SteamVR/OpenVR, solver, body/world transforms, Hub automation, third-party
RE source, Wi-Fi-only or count-limit bypass was added.

Task 4 owns physical-ID mapping, consumer freshness/session retirement, verified native coordinate
conversion, normalization, calibration/mount transforms and output policy. MonakaVR owns roles,
fusion/fallback/IK. Keep the contract candidate until master reconciliation.

## Configuration and operation

Copy config/vut-service.example.conf to persistent installation configuration. Replace source ID
with a stable ID unique to this installation. Set each full lowercase MAC's actual map ID; use
different map IDs unless a shared origin is physically verified. Unknown MACs are rejected.
IDs cannot contain spaces in this simple format. 256 configured devices is a memory bound, NOT
a claim about supported hardware count. Source ID + full MAC is identity; slot/index never is.

Run: build/Release/vut_service.exe CONFIG REVISION_JOURNAL [PORT]. Default target 127.0.0.1:29810;
source socket binds loopback ephemeral port. Bridge owns ingress. Ctrl+C stops. Keep the same
config and journal on every restart; never delete/replace the journal while retaining map identity.
The journal is exclusively opened, append-only and flushed before publishing each new revision.
Corrupt/torn journals fail closed. Every service/dongle session has a new UUID, epoch and transport
generation. By default, `reconnect_revision strict` also gives every backend start a new revision.
Experimental `reconnect_revision preserve` may retain the revision only for a plain transport
reconnect within the same process and exact configured source/device/map identity. It never edits
or rolls back the journal; the next required advance appends the next number. Gaps
in revisions after an unused reservation are safe. Do not deploy duplicate source IDs using
separate journals; journal locking only prevents concurrent reuse of the same journal.
--check-session CONFIG JOURNAL reserves a revision without opening HID, for configuration tests.

HID read errors end the session and trigger reconnect attempts; timeouts alone do not. An exact
configured device returning after an observed unpair starts a new session. Strict mode advances;
preserve mode may retain the revision as the operator-selected reconnect case. Unknown RF addresses
remain rejected and are never rebound. Ambiguous/uncertain recovery and sequence exhaustion are
unsafe and advance. Stop/restart after map reset or ambiguous RF gap; no trusted automatic reset
detector exists. Process/config
lifetime changes advance. Undetectable map resets require operator restart. U63 exhaustion
requests a session restart; revision exhaustion requires provisioning a new map identity/journal.
For diagnostics without C1 use vut_probe or -DVUT_BUILD_OBSERVATION=OFF. Tests do not open HID.

## Receipt, streams and limitations

Monotonic receipt time is captured once immediately after HID read, before parsing/control work.
Service subtracts its monotonic epoch; sent_at is stamped at encode in that clock. No wall clock.
Generation and epoch reject retained events from prior sessions. Metadata does not restamp pose.
Consumers must use C1 age/sequence/session rules and 500 ms pose timeout even with missing unpair.

The original decoder had no duplicate/reorder policy. C1 uses 8-bit half-range serial arithmetic:
delta 1..127 admitted, 0 duplicate, 128..255 old/ambiguous. 254->255->0 remains the same session
and advances an independent U63 pose sequence. Identical numeric pose remains fresh on admission.
A gap >500 ms since last accepted pose fails closed until explicit service/session restart;
metadata does not reset this history. This sacrifices automatic recovery after ambiguous loss.
Sensor age, driver buffering and arbitrary replays delayed by a full counter cycle cannot be
proven from a short counter. Receive timestamps are not device sample timestamps. No unconditional
replay/liveness guarantee is claimed; hardware counter and reconnect characterization remain due.

Pair/unpair has a separate sequence. Unpair cancels pending pose and blocks new pose until
session renewal. ACK/buttons/raw RF remain diagnostic only. Counters never become C1 identities.

## Mapping / native space profile

| Status | Position | Orientation | C1 state |
|---|---|---|---|
| 2 | finite vector | finite norm 0.5..1.5 | tracked if both, degraded if one, lost if none |
| 3/4 | null/invalid | same quaternion check | degraded if valid, lost otherwise |
| 0/1/5 | null/invalid | null/invalid | initializing |
| other | null/invalid | null/invalid | unknown |

Quaternion retains existing xyzw values without normalization, transform or further reorder.
Evidence is device for valid orientation, none otherwise. Only position/orientation capabilities.
Battery and all derivatives are null, including four raw angular values and raw acceleration.
Position metres, device-to-space quaternion interpretation, axes and shared maps are NOT hardware
verified. vut-native-v1 is a profile key, not an identity transform to OpenVR/MTP. space.id is
explicit per MAC; revision increments conservatively unless the narrow transport-reconnect opt-in
applies. No world/body calibration here.

## Publication

Single polling-thread ownership. Nonblocking UDP send, bounded latest per (device,type) queue:
512 entries maximum, default flush budget 8, <=4096-byte C1 packets. Absent removes pending pose;
session end discards all pending data. Encode/send errors are isolated and counted. Failed sends
are dropped; old samples are not replayed as fresh when a consumer returns. UDP send success is
not acknowledgement. Injected Send callbacks must be nonblocking. Counts print at session end.

## Decoder fixes and verification

- c371f5f: MSVC rejected GCC-only __builtin_bswap names in a discarded constexpr branch.
  Local byte assembly preserves little-endian reads without compiler intrinsics.
- 1b2b172: independent half subnormal correction, 127-15-shift -> 127-14-shift. 0x0001
  failed in Release before fix; now 2^-24 and max subnormal pass, along with signed zero,
  normal boundary, +/-Inf and NaN. CHECK exceptions replace assert so NDEBUG cannot hide tests.
- vut_protocol_tests --negative-fixture deliberately expects 0x0001 -> 0, prints failure and
  exits 1 in Release. This was executed and the exit code checked.
- HID cancellation now completes before freeing OVERLAPPED/buffer/event. Read error and timeout
  are distinct; error clears caches. Source-reviewed and Windows-built, hardware NOT RUN.

Windows 11 x64 / MSVC 19.51.36257.0 / SDK 10.0.26100.0 / CMake 4.3.1-msvc1 /
Visual Studio 18 2026 generator; Python 3.13.15; Microsoft JDK 17.0.20.1.
Build: cmake -S . -B build -G "Visual Studio 18 2026" -A x64, then
cmake --build build --config Release and ctest --test-dir build -C Release --output-on-failure.
CMake found old Java 8 initially; explicit Java_JAVA_EXECUTABLE and Java_JAVAC_EXECUTABLE paths
selected JDK 17. The test harness explicitly reads/writes UTF-8 (Windows default encoding initially
failed). Compiler discovery needed reviewed host execution outside the sandbox.

PASS: Release backend/probe/service/codec/mock compilation; C1-disabled diagnostic build/test 1/1;
fixed codec 74 fixtures, shared C++/JVM directions 44, seven VIVE status samples C++->JVM->C++;
37/39-byte variants/truncation/status/nonfinite/zero quaternion/MAC/slot reuse/pair/unpair;
wrap/duplicate/reorder/unchanged pose/session retirement/two sources/metadata-only/event omission;
10000 failed sends, queue cap, encode recovery, real loopback receiver absent/stop/reopen;
persistent monotonic revision/new UUID/torn and corrupt journal/duplicate config tests; strict
default, preserve reconnect, unsafe restart, identity change and exact-RF no-rebind policy tests.
Final full-suite counts and exact commands are recorded in dist/task3-final-report.json.

NOT RUN for this implementation change: Linux; ALL hardware checks (Hub stopped dongle open, one/multiple trackers, optical
loss/recovery, unpair/power-cycle, dongle reconnect, map reset, 1 m translation, axis/quaternion).
No Hub-free physical operation is claimed. Wi-Fi-only/count bypass is outside scope.

Policy-design evidence supplied from a separate 2026-09-21 HIL run is deliberately narrow:
power OFF/ON and re-pair retained source `vive-local-1`, RF address `23:32:03:81:fe:ec`, and map
`tracker-0-map`; strict mode changed revision 55 to 56; physical fixed-point pose comparison was
broadly consistent. This does not establish that VIVE guarantees map continuity. Preserve-mode
transport reconnect, explicit map reset/reprovision, ambiguous RF recovery and broader hardware
coverage remain required gates.

## Handoff and open questions

python scripts/package_handoff.py generates dist/vive-backend-handoff.zip with fixed kit/metadata/
lock, synthetic binary fixtures, C1 JSON samples, generic consumer sources, config and this guide.
External manifest records per-file hashes and source commit. No live sibling checkout is needed.
Fixtures are synthetic regressions, not device captures. Fixed Task 1 ZIP hash:
eef5b7f2bc490926385b99dabcd44dc5a374228bf2a7869beea01f9dad936729.
Source/schema/C1/lock hashes remain in dependencies/monaka-protocol.handoff.json.

Unresolved: battery, derivative units/frame/gravity, map-transfer recovery, exact native coordinate
profile, map identity/sharing, arbitrary delayed RF replay and hardware liveness. These require
hardware evidence; do not invent conversions or claim them as validated.
