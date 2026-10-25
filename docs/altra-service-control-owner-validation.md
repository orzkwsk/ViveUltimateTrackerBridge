# Altra Passive Service-Owner Integration — Step 2 receipt

Date: 2026-10-09 JST. Repository: `orzkwsk/ViveUltimateTrackerBridge`.
Branch: `feature/altra-service-control-owner`.
Base / reviewed Step 1 HEAD: `7ac12198a5c785f79f09b0d80139a41a844468f5`.
Implementation commit: `eac1b56798247947bfae48c6e2631dd4d49f34e0`.
Tests and this receipt are committed immediately afterward on the same branch.
Remote refs were checked before implementation: `feature/altra-control-model`
matched that exact SHA, and `refactor/monaka-layer-separation` remained at
`a594e4377ef2b5cca04e915187b0c12e59064839`. Case A selects the reviewed feature
HEAD. No merge/rebase/cherry-pick or other repository change is performed.
Task authority: the user's Step 2 request, with the supplied instruction as SoT.
Instruction attachment SHA256:
`3847cb46520fc3851ffd8f713ede06abecdc80dc619ca509968bf7553f968f12`.
Read with the [architecture](altra-service-control-architecture.md),
[work breakdown](altra-service-control-work-breakdown.md),
[reference audit](altra-service-control-reference-audit.md), and
[Step 1 receipt](altra-control-model-validation.md).

## Implemented owner boundary

`ServiceRuntime` owns Backend, ServiceControlModel, the existing session/revision
coordinator, and session-local adapter/publisher dependents. The `vut_service`
main thread constructs, starts, polls, reads, reconnects and destroys that runtime.
There is no new worker/executor thread. Owner-thread checks reject calls from another
thread, and guards reject recursive start/poll/shutdown. Backend and mutable model
references do not escape. The runtime is noncopyable and nonmovable; destroy it on
its owner thread. Destruction on another thread terminates rather than operate HID
from an unauthorized executor.

Exactly one stable Backend callback is registered for the runtime lifetime. It
captures the runtime, not session-local references. Reopen does not replace it;
while transport/session is inactive it ignores events. During final/fault shutdown:
reject new work, retire model handles, clear callback, stop Backend, clear publisher,
then destroy adapter/publisher dependents. The Backend object is destroyed last.
On ordinary reconnect the old dependents remain alive through stop and are released
before creating the next session; the still-registered dispatcher cannot enter them
because its accepting gate is closed. Explicit tests cover shutdown and exception paths.

Dispatcher ordering is:

1. Check actual Backend generation, session epoch and ordered monotonic receipt.
2. Update current registry binding/reception using the immutable Backend receive time.
3. Call the actual existing adapter.
4. Record its actual pose admission, with the same receive timestamp.
5. Enqueue any admitted envelope into the existing publisher.

There is no controller/provider observation stage connected. The small
`ServiceObservationStage` component factory permits test instrumentation and injected
adapter exceptions; production wraps ViveObservationAdapter without changing it.
Admission uses its returned TrackerObservation or actual unknown-device rejection
counter; no serial, RF-counter or numerical pose value invents an admission result.
Raw/accepted timestamps are mirrored through the existing pure-model pose API in two
stages, with a scoped event clock. Metadata and rejected/malformed/unconfigured poses
cannot refresh accepted time. Malformed unbound input cannot create a new handle.
Reception freshness remains independent of optical validity and publication success.

The runtime keeps at most five current address-routing entries, separate from history.
RF/slot are never persistent identity. Explicit pair always creates a new epoch;
slot reuse and same-RF slot moves retire old handles. Pose-first conflicting ownership
is quarantined. A late unpair for an old RF cannot retire a new slot occupant. Retired
generation/epoch events reach only the adapter's existing rejection counters; impossible
future/globally retired timestamps cannot contaminate its current stream.

On open success the model receives the actual Backend transport generation. On
read loss it immediately retires handles after Backend returns failure. **Existing
Backend::pollOnce internally stops before returning a read failure**; this order is
preserved rather than claiming a new pre-close notification or adding a vendor event.
Runtime-owned stop/shutdown first retires the model. Adapter-required restart advances
the control session, clears old publication state and chooses the existing strict/
preserve coordinate policy through the unchanged SessionRevisionCoordinator.
The runtime snapshot associates its local control session with the actual wire session
UUID/generation/revision; these remain separate contracts.

`snapshot()` returns a detached `shared_ptr<const ServiceRuntimeSnapshot>` containing
the const control snapshot, typed status/failure, actual session and copied diagnostics.
Reads do not mutate registry state. Future readers must obtain the snapshot on the owner
executor and pass the detached value to workers; the live model is not made thread-safe.

Callback work contains no Backend lifecycle/feature call, send/flush, logging,
filesystem, sleep, client callback or recursive poll. Optional diagnostics are capped
at eight events with 512-byte text and drained/logged after poll. Current lookup uses a
fixed array, not full snapshots. The one narrow Step 1 change removes its per-validation
temporary ID tree allocation: append-only record IDs are checked for strict monotonic
order instead. All index/uniqueness checks, capacity and fail-closed behavior remain.

## Deliberate changes and equivalence evidence

Runtime behavior intentionally changes: passive control mirroring, explicit owner/
lifetime boundaries, post-poll bounded diagnostic logging, and visible terminal runtime
faults on core model/dispatcher inconsistency. Fault teardown waits for the current poll
to return. Start/factory/publisher construction failure, adapter/callback and poll
exceptions cannot leave a callback targeting destroyed dependents. Existing publisher
send/encode failures remain isolated counters and do not stop normal polling.

Normal emitted Observation bytes, decoder/mapping, coordinate/session selection,
publication queue/budget, reconnect policy and vendor commands remain equivalent:

- The runtime test replays identical timestamped events through the reviewed legacy
  adapter/publisher composition and the new runtime, comparing **exact encoded bytes**.
  It covers pair/unpair/re-pair restart, tracked/lost/degraded/unknown, invalid quaternion,
  duplicate/reordered/wrapped RF counters, gap rejection, ACK and unconfigured RF.
- A real Backend with in-memory HID transport compares **every feature packet byte**
  between legacy Backend polling and ServiceRuntime polling with default configuration.
  The fixture includes configure-on-pair, a decoded pose, NAZZ -> FW1 and slot-local
  unpair RxPowerSaving(false). Feature sequences match exactly; no PairDevice enable,
  RestartRf, NA/identity query, power/reset or new setup command is introduced.
- Strict and opt-in preserve reopen/re-pair revision results match the existing policy;
  an injected continuity-uncertain restart still advances revision under preserve.
- A 2,000-pose synthetic flood advances the actual publisher on every poll, with one
  Observed record, no pending backlog and no HID writes. This is software progression
  evidence, not measured hardware latency, multi-tracker operation or a throughput guarantee.

Pairing/Identify/identity-acquisition capabilities are compile-time false. No operation
mutation entry point exists in ServiceRuntime; PairingController/Identify/provider are
not connected. Active diagnostic SLAM-query config is rejected. Production shared-open
default remains unchanged; no exclusive-HID cutover or launcher change is delivered.
Identity on current unambiguous bindings is Unavailable/ProviderNotInstalled; retired/
ambiguous history retains Step 1 status truth. No Candidate/Verified acquisition occurs.

## History exhaustion and ownership blockers

Both an injected two-record limit and the actual default **256-record limit** are tested.
The next fresh binding faults the registry; RuntimeSnapshot exposes Registry failure and
the original HistoryFull outcome, all handles become non-actionable, and ordered teardown
closes the runtime. start() refuses reopen in that same faulted instance. `vut_service`
logs the typed fault/history-full flag and exits 1. No reset, rebind or eviction masks it.
This adds a finite service lifetime under repeated connection boundaries and is an
explicit **production cutover blocker**, not a long-running production readiness claim.

Design-only follow-up: an explicit expiry policy could remove retired, non-actionable
records without retained operation references after a declared age, without reusing
tokens/epochs. Retention, client-history guarantees and fault recovery require owner
review before implementation. No compaction/eviction is implemented in this task.

Instance-wide ownership lock is **NOT IMPLEMENTED**. Existing revision-journal file
exclusivity only protects that file, not services using different journal paths. Proposed
follow-up is a Windows instance lock keyed to provisioned installation/dongle identity,
with explicit account/session policy, fake and real Windows contention tests. Named mutex
or exclusive file selection and deployment principal remain review gates; no pipe exists.
Actual dongle/Hub contention and exclusive-open qualification remain HIL_REQUIRED.

## Validation actually executed

Windows x64; Visual Studio 18 2026, MSVC 19.51.36260.0, Windows SDK 10.0.26100.0;
Python 3.13.15; Java 17.0.20 explicitly selected. CMake/CTest used the Visual Studio
`Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin` installation.
Build/receipts: ignored `build/altra-service-control-owner/`.

```text
cmake -S . -B build/altra-service-control-owner -A x64 -DBUILD_TESTING=ON -DVUT_BUILD_OBSERVATION=ON -DJava_JAVA_EXECUTABLE="C:/Program Files/Microsoft/jdk-17.0.20.101-hotspot/bin/java.exe" -DJava_JAVAC_EXECUTABLE="C:/Program Files/Microsoft/jdk-17.0.20.101-hotspot/bin/javac.exe"
cmake --build build/altra-service-control-owner --config Release --parallel
ctest --test-dir build/altra-service-control-owner -C Release --output-on-failure --no-tests=error --output-junit owner-sandbox-results.xml
ctest --test-dir build/altra-service-control-owner -C Release --rerun-failed --output-on-failure --output-junit owner-host-rerun-results.xml
ctest --test-dir build/altra-service-control-owner -C Release -R "^(service_runtime|service_configuration)$" --output-on-failure --output-junit owner-final-results.xml
python scripts/test_release_v2.py
python -O scripts/test_release_v2.py
```

- Configure: **PASS**, including actual pinned v2 kit and historical v1 integrity.
- Windows Release full/final build: **PASS**. One intermediate incremental link failed
  LNK1104 because this task overlapped test execution with relinking the same EXE.
  The execution order was corrected; sequential updated and final builds passed.
  The failure is retained in the task command transcript, not counted as success.
- Initial selected native tests: **PASS 3/3**, receipt `owner-initial-results.xml`.
- Full sandbox CTest: **PASS 14/19, FAIL 5/19**, 43.10 s. Failed pre-existing tests:
  setup_assignment (atomic replacement), assignment_lab_cli/assignment_lock_process/
  setup_cli (sandbox temporary-file permissions/cleanup), loopback_udp (receive timeout).
- Host rerun of **exactly those five failures**: **PASS 5/5**, 2.45 s. Thus all 19
  distinct CTests passed across runs, including all 18 existing tests and the new runtime
  test. Final runtime/configuration tests after the final audit/build: **PASS 2/2**, 1.71 s.
- Existing C++/Java cross-language validation: **PASS**, 24.86 s in the full run.
- Existing release tooling: **PASS 13/13** normally and **PASS 13/13** with `-O`.
- The new runtime tests additionally cover owner/reentrancy rejection, snapshot isolation,
  receive/accepted separation including malformed input, transport/session handle loss,
  late events, ABA boundaries, callback clearing, start/poll/adapter/publisher construction
  failures, consumer exceptions, both history limits, regressed-clock fault propagation,
  and bounded synthetic publisher progress. Assertions are explicit CHECKs in Release.
- JUnit failure receipts and logs remain in the ignored build directory. No unresolved
  software failure remains. Whitespace and scope checks: **PASS**.

## Scope audit and stop gate

Files changed: new `include/vut/service_runtime.hpp`, `src/service_runtime.cpp`,
`tests/service_runtime_tests.cpp`, this receipt; `tools/vut_service.cpp`, CMake registration,
and the allocation-free registry consistency check. Backend/protocol/adapter/publisher/
session coordinator code, pairing/setup, launchers, third-party kits and manifests are
unchanged. No IPC/Named Pipe, body assignment, identity provider or MonakaProtocol/
MonakaBridge/MonakaVR changes. No other repository modified/committed/pushed/cherry-picked.
Hardware open, pairing operation, Hub operation, HIL, physical latency: **NOT RUN**.

Software integration gate: **PASS**. Production cutover: **NO-GO** because of finite
history, absent instance-wide ownership qualification, unchanged shared HID default,
unqualified physical tracking/contention and later operation/crash-cleanup policy.
Stop here. Subsequent Step 3 lifecycle/controller work or Step 4 IPC needs separately
scoped authorization; do not enable them as a continuation of this Step 2 task.

Push only `feature/altra-service-control-owner`. Integration branch merge: not performed.
Recommend review of this software integration before further stages. Preserve unrelated
untracked `.codex-remote-attachments/` and `hil-private/`; build outputs remain ignored.
