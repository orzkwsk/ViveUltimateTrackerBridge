# Altra Pure Control Model — Step 1 receipt

Date: 2026-10-09 JST. Repository: `orzkwsk/ViveUltimateTrackerBridge`.
Branch: `feature/altra-control-model`.
Base: `ab1eeb596e450480c48ba876be1f7ef153c6c725`.
Read-only remote fetch showed `refactor/monaka-layer-separation` still at
`a594e4377ef2b5cca04e915187b0c12e59064839`, without the design commit.
The supplied instruction therefore selects the design commit directly, which
derives from that prepared lineage. No integration-branch merge is performed.
Instruction attachment SHA256:
`2b21a889e3235273fd00747a14d6a24969438477e8fc1b3ef4cee3f782ceb49e`.
Primary SoT: [architecture](altra-service-control-architecture.md),
[work breakdown](altra-service-control-work-breakdown.md),
[reference audit](altra-service-control-reference-audit.md).

## Delivered boundary

`include/vut/device_registry.hpp`, `src/device_registry.cpp`,
`include/vut/service_control.hpp`, `src/service_control.cpp` introduce the independent
`vut_control_model` library. It links no Backend, HID, setup, pairing, observation,
IPC or shared protocol target. Only two software test executables link this library.
Existing `Devices` and all existing runtime targets/behavior remain unchanged.

- ServiceControlModel is a noncopyable single-writer model, with an injectable
  service-local monotonic clock. It creates no executor thread or runtime wiring.
- A fresh model obtains a random service namespace. RuntimeHandle is opaque,
  with a random nonce and a non-reusable ordinal inside that namespace. Its registry
  binding contains service instance, observation session, transport generation,
  connection epoch, current RF and slot. Tokens are not authorization credentials.
- RF/slot index only current ephemeral records. History cannot be used for a
  mutation. Generation values must increase within an instance; reuse in a new
  instance cannot revive a handle. New transport starts also advance local session.
- Explicit pair boundaries always issue a new epoch. Slot reuse and same-RF slot
  moves retire the old handle and evidence. Conflicting ownership quarantines both
  bindings; pose-first conflicting discovery cannot silently replace ownership.
- Typed Outcome contains an ErrorCode and Reason; validation never branches on
  exception text. Invalid arguments/transitions leave state/revision unchanged.
  Invariant/counter faults deliberately quarantine control state and latch faulted.
- OperationId is also instance-scoped and opaque, preventing a cached old operation
  ID from addressing an identically numbered operation in a new service instance.
  Pure Queued/Running/Succeeded/Failed/Cancelled/TimedOut transitions do no work on
  a device. One record may run at a time; stale queued targets are retired/rechecked.
- Terminal primaryResult cannot be replaced. cleanupResult/current status, first
  cleanup failure and attempt count are separate. Later confirmed cleanup updates
  cleanup truth while preserving the original primary and first cleanup failure.
- Snapshots are detached `shared_ptr<const ...>` values, including revision, scope,
  capture time, device/operation data and typed warnings. They survive mutation or
  owner destruction; an old snapshot's token still fails current validation.
- Last seen, raw pose reception and accepted pose times are independent. Metadata,
  rejected/unconfigured poses and snapshot reads do not advance accepted pose time.
  Freshness threshold/policy is injected; absent policy yields Unknown after a pose.
  Tracking state/raw status is supplied by a caller, without new vendor semantics.
- Identity starts Unavailable/ProviderNotInstalled; retirement makes it Stale and
  ownership ambiguity makes it Ambiguous. Candidate/Verified are reserved states;
  only a private test fixture injects Candidate. No path generates Verified or
  acquires stable identity; re-pair never inherits old identity evidence.

Default budgets: 256 total current/history records, five current slots,
64 retained operations, 32 queued operation records. Terminal operations can be
explicitly expired, removing their references; active/uncertain-cleanup records
cannot be evicted. No automatic replay or eviction is implemented. Record/history
exhaustion latches a fault and retires actionable handles; operation retention/queue
exhaustion returns Busy without eviction. Counter/revision overflow never wraps and
requires a fresh service instance to recover. Smaller budgets/ceilings support tests.

## Tests and actual results

New CTests: `device_registry`, `service_control_model`.
Tests cover service namespace/generation reuse, instance/session/generation/loss
boundaries, disconnect, repeated pair, slot reuse, same-RF slot change, old queued
requests, ambiguous/duplicate ownership, corrupted indexes, metadata versus raw and
accepted pose ages, immutable snapshots including owner destruction, identity
retirement, primary versus repeated cleanup failure and later confirmation,
cancel versus timeout, queue/current/history/operation limits, epoch/session/
generation/revision/operation/cleanup-attempt exhaustion, regressed clock, and
invalid transitions without partial mutation.

Environment: Windows x64, Visual Studio 18 2026, MSVC 19.51.36260.0,
SDK 10.0.26100.0, Python 3.13.15, Java 17.0.20 explicitly selected.
CMake/CTest resolved to the Visual Studio
`Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin` installation.

```text
cmake -S . -B build/altra-control-model -A x64 -DBUILD_TESTING=ON -DVUT_BUILD_OBSERVATION=ON -DJava_JAVA_EXECUTABLE="C:/Program Files/Microsoft/jdk-17.0.20.101-hotspot/bin/java.exe" -DJava_JAVAC_EXECUTABLE="C:/Program Files/Microsoft/jdk-17.0.20.101-hotspot/bin/javac.exe"
cmake --build build/altra-control-model --config Release --parallel
ctest --test-dir build/altra-control-model -C Release --output-on-failure --no-tests=error --output-junit model-sandbox-results.xml
ctest --test-dir build/altra-control-model -C Release --rerun-failed --output-on-failure --output-junit model-host-rerun-results.xml
ctest --test-dir build/altra-control-model -C Release -R "^(device_registry|service_control_model)$" --output-on-failure --output-junit pure-final-results.xml
python scripts/test_release_v2.py
python -O scripts/test_release_v2.py
build/altra-control-model/Release/vut_device_registry_tests.exe --negative-fixture
```

- Configure: **PASS**, including existing actual v2 artifact/manifest/source/contract/
  content verification and historical v1 provenance; no dependency content changed.
- Initial sandbox build: stalled at Checking Build System; interrupted, exit 1.
  No compilation success claimed. Host Release build and final updated build: **PASS**.
- Initial added tests: **FAIL 0/2**, retained as `pure-initial-results.xml`.
  The session-ceiling fixture needed matching record scope, and the queue-limit
  fixture attempted enqueue while its single queued slot was still occupied.
  Corrected fixtures and final Release added tests: **PASS 2/2**, 0.09 s.
- Full sandbox CTest: **PASS 13/18, FAIL 5/18**, 25.93 s. Failed existing tests:
  `setup_assignment` (atomic replacement), `assignment_lab_cli`,
  `assignment_lock_process`, `setup_cli` (sandbox temp write/cleanup permissions),
  `loopback_udp` (sandbox loopback receive timeout).
- Host rerun of exactly those five: **PASS 5/5**, 0.92 s. All 18 distinct software
  tests therefore passed across the runs, including all 16 pre-existing CTests.
  Final pure-model changes affect no existing target; both added tests were rerun
  after the final build. No unresolved software failure remains.
- Existing C++/Java cross-language test: **PASS** in the full run.
- CI-equivalent release tooling: **PASS 13/13** normally and **PASS 13/13** with `-O`.
- Deliberately broken stale-handle expectation: **expected FAIL, observed exit 1**.
  This proves the explicit CHECK remains active in Release, independent of NDEBUG.
  Receipt: `build/altra-control-model/negative-fixture-receipt.txt`.
- JUnit/log receipts remain in ignored `build/altra-control-model/`; sandbox failures
  are retained, not relabelled as passes. Build interruption also remains in the
  task command transcript. Whitespace/scope audit: **PASS**.

## Scope audit and next gate

Changed files are the four pure model files, two test files, CMake registration and
this receipt only. Runtime behavior diff: none. HID/protocol/pairing packet diff: none.
IPC: not implemented. StableIdentity provider: not connected. MonakaProtocol,
MonakaBridge, MonakaVR and other repositories: not modified. Body-role/persistent
assignment: not implemented. Hardware/query/pairing operation/HIL: **NOT RUN**.
Only existing offline/fake-transport/loopback software regressions were executed.

Next authorized implementation would be Step 2 on
`feature/altra-service-control-owner`: one passive owner executor and callback
dispatcher, registry-first ordering with actual adapter admission recorded,
session/loss retirement, shutdown/exception ordering, fake adapter/publisher and
flood tests. Exclusive-open/instance-lock platform tests and actual dongle/Hub
contention/tracking HIL are later gates. Keep pairing/Identify mutation capability
off, no startup PairDevice/query, provider Unavailable, and existing session/map
revision policy intact. This task stops before that runtime integration.

Remaining decisions: production token serialization/entropy qualification and IPC
authorization are later API work; no token is an access credential. Operation
retention expiry/owner lease/idempotency and durable cleanup need coordinator/IPC
review. A handle prevents client ABA, not hidden vendor late-packet identity reuse.
Physical replacement, concurrent pairing latency/recovery, identity acquisition
and upstream assignment contracts remain unqualified.

Working-tree policy: preserve untracked `.codex-remote-attachments/` and
`hil-private/` without staging or modification. Build outputs remain ignored.
Push only the feature branch. Integration merge: not performed. Recommendation:
review Step 1 before passive service-owner integration; production cutover NO-GO.
