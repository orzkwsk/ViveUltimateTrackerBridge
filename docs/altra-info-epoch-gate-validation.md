# Altra Stage 1: connection epoch / info collector gate validation

Stage 1 result: **PASS (software / fake transport)**. Hardware HIL: **NOT RUN**.
Implementation branch: `fix/altra-info-epoch-gate`.
Core implementation commit: `407a00adef307344820d87afb868952ea0660034`.

Scope follows the user-supplied Stage 1 Info Epoch Gate instruction. No live
HID open, tracker/dongle operation, official runtime operation or new capture
was performed. Production executables were compiled, not run against hardware.

## Stage 0 integration provenance

Before Stage 1, `git fetch --no-prune origin` confirmed both local and origin
refs at the specified checkpoints:

- Integration `refactor/monaka-layer-separation`: `a594e4377ef2b5cca04e915187b0c12e59064839`.
- Stage 0 `fix/altra-wire-exchange-evidence`: `fd326202ed444d6b7566ed7f1b46eee8854ffd49`.

The dedicated managed worktree was clean, the Stage 0 validation note existed,
and `git merge-base --is-ancestor` verified the descendant relationship.
Stage 0 was merged with `git merge --ff-only`, then normally pushed. No merge
commit or force push was used. Local integration, origin tracking and live
remote HEAD all equaled `fd326202ed444d6b7566ed7f1b46eee8854ffd49` after push.
Stage 1 was then created from that exact commit. The primary checkout's
unrelated branch and untracked attachments were preserved.

Research checkpoint `4560a086541b32102a0a0e962a68a89eada54d63` was read-only:
reconciliation report, official runtime report and sanitized sequence JSON.
The two existing private CSVs were read only to check token suffix shapes;
their hashes were unchanged before/after. No research merge/cherry-pick,
third-party implementation copy, raw capture or identity value enters this branch.

## Transient connection model

Private key: **transport generation + connection epoch ID + full six-byte
current destination + slot**. Slot is addressing metadata; destination and
epoch are transient runtime data, never stable identity or persisted config.
The epoch counter is independent of the HID-open generation counter.

Every parsed DevicePaired starts a new epoch, including the same destination
on the same transport generation. An active previous epoch at that slot is
closed as Superseded; progress, mask and pose evidence are never reused.
Storage is bounded to the current and immediately previous snapshot per slot
(at most ten snapshots). Only the current record keeps private matching data.

Phases are Associated -> InfoCollecting -> InfoComplete -> PoseObserved,
with Disconnected/Invalidated terminal phases. Associated -> InfoCollecting
occurs in the same receive processing step. No Ready field or inference exists.
The read-only `Backend::connectionEpochSnapshot()` exposes epoch ID,
transport generation, slot, phase, progress, seen mask, active/info complete,
initialization eligibility, pose observed and reason. It exposes no full RF,
DeviceId, serial, NADS/NASS payload or shared-protocol field.

Matching DeviceUnpaired, stop, transport loss, generation change, supersession,
protocol-order violation and counter exhaustion close/invalidate an active
epoch. Read exceptions invalidate connection evidence then preserve the
existing caller-owned exception/cleanup path. A later stop/late input cannot
rewrite the first failure reason or revive closed evidence. Reopen requires
a new DevicePaired before any new info collection.

Epoch IDs are monotonic uint64 within a Backend instance. UINT64_MAX can be
the last allocated ID; the next allocation fails closed, invalidates active
epochs and latches exhaustion across transport restarts. No wrap is allowed.
Progress is bounded to nine; index/mask/state consistency is checked before
any shift or increment. Synthetic corrupt/exhausted progress is Invalid.

## Exact info burst and recognition policy

Expected order:

`NANA -> NADS -> NASS -> NASI -> NAPI -> NAV1 -> NARI -> NAGN -> NAZZ`.

This is a gate for the two saved current official B profiles, **not a general
vendor specification**. Both captures showed the same suffix shapes:

| Token | Suffix bytes | Minimum checked shape |
|---|---:|---|
| NANA | 3 | Printable ASCII |
| NADS | 12 | Printable ASCII |
| NASS | 13 | Printable ASCII |
| NASI | 8 | Decimal digits |
| NAPI | 8 | Printable ASCII |
| NAV1 | 11 | Printable ASCII |
| NARI | 1 | Decimal digit |
| NAGN | 5 | Printable ASCII |
| NAZZ | 0 | Exact token, no suffix |

Contents are not retained by the collector. Other shapes/firmware profiles
fail closed pending separate evidence. `NAZZgarbage`, terminators, truncated
known payloads and nonprintable content are rejected. The existing Ack event
raw-text compatibility remains unchanged, without adding raw values to snapshots.

- Duplicate (including a repeated terminal NAZZ): Invalid, no eligibility.
- Reorder/missing intermediate token: Invalid at the first unexpected token.
- Missing final NAZZ: remains incomplete, with no eligibility, until a closing
  event. No unsupported timeout or end-of-stream assumption was invented.
- Late after disconnect/invalidation: frozen snapshot, no collector update.
- Same slot/different full destination: DestinationConflict, old progress not
  consumed and no eligibility. All six bytes are compared, not a partial MAC.
  A conflicting pose or unpair input is likewise fail-closed, not a matching
  disconnect/pose for the previous destination.
- Ordinary non-N ACK: no progress change. Unknown N* invalidates any active
  epoch, including a previously Complete epoch; this strict policy is tested.
- Stale generation: ignored, never moved into the new generation.

The wire has no connection-epoch marker. Tokens delivered after a new
DevicePaired with the exact same full destination cannot independently prove
causal freshness or distinguish a complete replay from a new identical burst.
The gate proves receive order/destination within the local epoch, not replay
authenticity. Previous epochs never acquire completion from those tokens.

## Eligibility, pose and event ordering

Initialization eligibility requires an active, matching epoch, an exact full
burst and no violation. It is evidence only and triggers no vendor command.
It does not mean initialized, Connected, Ready or Tracking.
Matching parsed pose arrival sets poseObserved, including Lost packets.
Any pose before InfoComplete records PoseBeforeInfo and permanently invalidates
that epoch; later NAZZ cannot normalize it. Existing pose parsing, state cache
and BackendEvent publication continue independently of this evidence gate.
No MonakaProtocol/Observation mapping or service IPC behavior was changed.

Mutations run on the existing poll/event path under the Backend mutex;
snapshots take that mutex. No worker or additional callback is introduced.
DevicePaired updates the epoch before Legacy configure writes and before its
callback. ACK updates the collector before Legacy FW1 and before its callback.
DeviceUnpaired updates/invalidates before its callback, then preserves the
original Legacy PWS-off-after-callback order. Fake callbacks/read-only snapshots
and pre-write hooks verify this order without mutex reentrancy deadlocks.

## Automatic vendor action policy and compatibility

`BackendConfig::automaticVendorActionPolicy` defaults to **Legacy**.
`configureTrackingOnPair` remains: under Legacy it disables only configure,
as before; FW1 and PWS-off retain their original semantics. Collector failures
do not alter Legacy sends, including the original prefix-based NAZZ -> FW1.

Explicit opt-in **ObserveOnly** suppresses configureOnPair, NAZZ -> FW1 and
DeviceUnpaired -> PWS-off, overriding the legacy bool. It also overrides the
diagnostic SLAM-query flag, so a Lost stream cannot generate automatic writes
after the five-second diagnostic boundary. Parse/epoch/info/pose/events continue.
Explicit beginPairing/endPairing and cleanup of a previous explicit operation
remain usable and retain Stage 0 semantics. ObserveOnly does not promise zero
writes if a caller explicitly requests pairing or carries pending pairing cleanup.
Passive HIL therefore requires no such operation/pending cleanup.

Existing Backend APIs and TrackerState/BackendEvent layouts remain. New config
field and snapshot accessor are additive; header consumers must rebuild for the
BackendConfig layout change. All repository targets were rebuilt. No new CLI
or service configuration/IPC option was introduced; opt-in is the BackendConfig
policy for a separately scoped diagnostic/HIL caller.

## Validation

Clean configure directory: `build/stage1-clean`. Windows x64 Release,
Visual Studio 18 2026, MSVC 19.51.36260.0, SDK 10.0.26100.0, Python 3.13.15,
explicit installed JDK 17.0.20. Fixed v2 artifact/manifest/source/contract/content
hashes and historical fixed kit integrity checks PASS. Neither contract was edited.

```powershell
cmake -S . -B build/stage1-clean -G "Visual Studio 18 2026" -A x64 `
  -DBUILD_TESTING=ON -DVUT_BUILD_OBSERVATION=ON `
  "-DJava_JAVA_EXECUTABLE=C:/Program Files/Microsoft/jdk-17.0.20.101-hotspot/bin/java.exe" `
  "-DJava_JAVAC_EXECUTABLE=C:/Program Files/Microsoft/jdk-17.0.20.101-hotspot/bin/javac.exe"
cmake --build build/stage1-clean --config Release --parallel
ctest --test-dir build/stage1-clean -C Release --output-on-failure --no-tests=error --output-junit stage1-results.xml
python scripts/test_release_v2.py
python -O scripts/test_release_v2.py
```

CMake/CTest used the installed Visual Studio bundled CMake bin. Clean configure
and full build PASS; the final test fixture changes were rebuilt in Release.
Full CTest **18/18 PASS**, including protocol, runtime_policy, backend_pairing,
setup_pairing, wire_exchange, new info_epoch, cross_language, observation,
service/session configuration and offline CLI/process checks. Release tooling
**13/13 PASS** in each normal/optimized run. Hardware HIL **NOT RUN**.

New tests cover every token omission, adjacent reorder and duplicate position,
late/retired events, all six destination-byte conflicts, same-slot supersession,
same-destination fresh association, generation restart, early pose at each
progress point, malformed shapes/NAZZgarbage, unrelated and unknown N ACKs,
epoch/progress exhaustion and bounded storage. Backend fake tests verify
publication/order, config combinations, read failure/exception/stop/reopen,
Lost arrival and explicit pairing compatibility. ObserveOnly automatic writes
are **0** for pair -> nine tokens -> pose -> unpair, and for a Lost stream across
the diagnostic due boundary; Legacy generates its original diagnostic queries.

Intentional Release negative fixtures both rejected with exit 1/CHECK failure:
`vut_info_epoch_tests --corrupt-epoch-fixture` and the unchanged Stage 0
`vut_wire_exchange_tests --corrupt-wire-fixture`. Checks remain active under NDEBUG.
Private NADS/NASS and RF leakage scan across all eight changed files PASS
(14 byte/text representations); the read-only private identity files were unchanged.

## Base diff / scope audit

| Gate against fd326202 | Result |
|---|---:|
| New vendor command | 0 |
| New payload kind | 0 |
| Changed default vendor payload | 0 |
| Changed default command order | 0 |
| New stable identity behavior | 0 |
| New service IPC behavior | 0 |

Protocol source/header, vendor types, PairingController, Stage 0 exchange model,
transport/HID/SetupAPI implementation, command builders, sendAck/configureOnPair/
PWS bodies, explicit pairing operations and literal wire tests are unchanged.
Default Legacy literal 65-byte regressions preserve Pair enable/disable,
host/client configure, kind04 TX, FW1 and slot-local PWS bytes/order.
New collector source has no vendor sender. ObserveOnly adds only explicit
opt-in suppression of existing automatic actions.

Changed files: CMakeLists.txt (source/test target), backend.hpp (policy/accessor),
connection_epoch.hpp (public metadata), detail/info_epoch.hpp and info_epoch.cpp
(private matching/pure collector), backend.cpp (receive/lifecycle/policy hooks),
info_epoch_tests.cpp (pure/fake matrix), and this validation note.

## Remaining work and merge recommendation

Stage 1 software is review-ready. This task does **not** merge Stage 1 into
integration or research. No stable identity, new official TX profile, behavior2/6,
kind03 sender, query1e, FW2, ATM21, MonakaProtocol or service IPC work was done.

Remaining P0 validation: passive single-B fresh-burst HIL has not established
that ObserveOnly receives DevicePaired -> exact nine tokens -> InfoComplete on
hardware. No automatic refresh/recovery is inferred when that burst is absent.
Remaining P1: separately scoped addressing/TX profile and official post-info
semantics/safety, followed by reconnect policy; all remain inactive/unimplemented.

Next task is **ObserveOnly + single B + existing official association passive
connection-epoch HIL**, with A OFF, no pending explicit pairing operation,
no NA/Pair/behavior2/6/kind03/FW2/ATM21 send and automatic vendor writes zero.
Freeze caller/build/policy and private evidence before that separately
authorized hardware task. Stage 2 is not started by this software PASS.
