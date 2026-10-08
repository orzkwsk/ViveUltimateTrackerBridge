# Altra pairing/assignment UX rework validation - 2026-10-09

Branch: `fix/altra-pairing-assignment-ux-rework`.
Starting implementation: `68af2d05ad8ef7cfceff624cff66c58569edd853`.
Original base: `e8ecd513391ab4f15af2941380631e7f05049bea` (ancestry verified).
No rewrite, merge to protected/base branches, related-repository changes or
external instruction-file import. [Read-only Phase 1 audit](altra-setup-rework-audit.md).

## Implementation decisions and changed files

| Files | Change |
| --- | --- |
| `include/vut/backend.hpp`, `src/backend.cpp`, `tests/backend_pairing_tests.cpp` | Shared bounded disable retry; exception cleanup eligibility; read-loss cleanup before close; no stale confirmed enable after transport generation change. Existing HID/feature/ACK/decoder paths preserved. |
| `include/vut/pairing.hpp`, `src/pairing.cpp`, `tools/vut_pair.cpp` | Shared PairingController state/result/time/event/cleanup lifecycle; standalone tool is a thin frontend. |
| `include/vut/setup_session.hpp`, `src/setup_session.cpp`, `include/vut/setup_console.hpp`, `src/setup_console.cpp`, `tools/vut_setup.cpp` | Setup control/model/presentation boundaries, same-session Pairing/listing/Identify, strict input parsing and sticky offline failure; interactive errors continue. |
| `include/vut/setup.hpp`, `src/setup.cpp` | Remove body-role vocabulary/model from setup; inject provisional IdentifyPolicy and expose evaluation metrics. StableIdentity provider receiver/fail-closed retained. |
| `include/vut/provisional_assignments.hpp`, `src/provisional_assignments.cpp`, `include/vut/assignment_store.hpp`, `src/assignment_store.cpp`, `tools/vut_assignment_lab.cpp` | Caller-supplied opaque label policy; internal namespace/target, legacy private experiment format only. Not a body-role SoT/storage/apply contract. |
| `include/vut/assignment_lock.hpp`, `src/assignment_lock.cpp` | OS exclusive handle / nonblocking flock ownership, stable lock path retained; crash release without manual stale-file deletion. Atomic replace and stale-writer checks retained. |
| `tests/setup_tests.cpp`, `tests/setup_cli.py`, `tests/setup_pairing_tests.cpp`, `tests/fake_pairing_transport.hpp`, `tests/assignment_lab_cli.py`, `tests/assignment_lock_process.py` | Policy/metrics, assignment isolation, fake-HID pairing exits/recovery, script failures, live command continuation, concurrent/crashed writer recovery. |
| `CMakeLists.txt`, `.github/workflows/release-tooling.yml` | Separate production model and provisional lab targets; register all new tests; native Windows/Ubuntu CI jobs with JUnit logs. Windows-only fake HID tests are not claimed on POSIX. |
| `scripts/setup_altra_trackers.ps1`, `README.md`, `docs/altra-setup.md`, `docs/altra-setup-rework-audit.md`, `docs/production-integration.md`, `docs/altra-setup-validation.md` | Launcher has no sidecar or external pairing detour; document responsibility, bring-up status, blockers, exact evidence and HIL checklist. |

Pair events reach the registry first; controller tick auto-disables after poll
returns and leaves HID open for Identify. Timeout/cancel/quit/signal/exception use
the same two-attempt cleanup, with Backend's one final attempt before close.
Transport read loss retries on the still-open handle. CleanupFailed is explicit;
pending disable is confirmed after reopen before any new enable. The old operation
stays failed, with cleanup status updated when recovery is confirmed. Ordinary
start/reconnect never enables PairDevice.

Body assignment is owned by MonakaVR. No authoritative role enum or usable apply
API was found in the inspected fixed Protocol/Bridge contract. The VUT role list
is removed; provisional Assignments/Store are kept only to preserve useful tested
storage/model techniques, outside the normal setup and launcher. Legacy v1 fields
have no production compatibility guarantee. Stable identity acquisition remains
unimplemented: mock serial restoration proves module behavior only. Direct
exclusive HID remains a stopped-service bring-up path; a service-owned setup API
and upper assignment adapter are implementation blockers.

## Local results

Environment: Windows x64; Visual Studio 18 2026 / MSVC 19.51.36260.0;
SDK 10.0.26100.0; Python 3.13.15; JDK 17.0.20. Release / NDEBUG.
New C++ checks throw and Python checks explicitly raise; they stay active in Release.

| Check | Result |
| --- | --- |
| Fixed v2 artifact/manifest/clean source/contract/content hashes | PASS, read-only audit, configure and CTest |
| Full Windows Release build (service/probe/pair/setup/codec/tests) | PASS |
| Full Windows host CTest | PASS 16/16, 19.55 s |
| Diagnostic-only Release build (`VUT_BUILD_OBSERVATION=OFF`) | PASS |
| Diagnostic Windows host CTest | PASS 10/10, 3.55 s |
| Fresh production configuration (`BUILD_TESTING=OFF`, lab OFF), setup/pair build | PASS; no lab project and no provisional library in setup link dependencies |
| Release tooling Python, normal / optimized | PASS 13/13 each |
| Launcher PowerShell syntax | PASS; actual launcher/interactive console/HID not run |
| Whitespace/diff review | PASS |
| Protocol decoder/adapter/publisher/service/session source changes | None |
| Physical hardware tests | NOT RUN |
| Remote CI | Native jobs configured; this receipt contains local evidence only. Actual run status is reported separately. |

Exact commands (from the repository root):

```powershell
$cmake = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
& $cmake -S . -B build/assignment-ux -A x64 -DBUILD_TESTING=ON -DVUT_BUILD_OBSERVATION=ON
& $cmake --build build/assignment-ux --config Release
& $ctest --test-dir build/assignment-ux -C Release --output-on-failure --no-tests=error --output-junit rework-host-results.xml
& $cmake -S . -B build/assignment-ux-diagnostic -A x64 -DVUT_BUILD_OBSERVATION=OFF -DBUILD_TESTING=ON
& $cmake --build build/assignment-ux-diagnostic --config Release
& $ctest --test-dir build/assignment-ux-diagnostic -C Release --output-on-failure --no-tests=error --output-junit rework-host-results.xml
& $cmake -S . -B build/assignment-ux-product -A x64 -DVUT_BUILD_OBSERVATION=ON -DBUILD_TESTING=OFF -DVUT_BUILD_ASSIGNMENT_LAB=OFF
& $cmake --build build/assignment-ux-product --config Release --target vut_setup vut_pair
python scripts/test_release_v2.py
python -O scripts/test_release_v2.py
git diff --check
```

Full build cache retains explicit Java paths:
`C:/Program Files/Microsoft/jdk-17.0.20.101-hotspot/bin/java.exe` and `javac.exe`.
PowerShell parsing used `System.Management.Automation.Language.Parser.ParseFile`
on `scripts/setup_altra_trackers.ps1`, with zero diagnostics. The launcher was not
executed because it would open real HID.

The sandbox run failed 5/16 (temp write/delete denial and loopback timeout); that
JUnit file is retained as `build/assignment-ux/rework-sandbox-results.xml` and is
not counted as PASS. The first host run exposed a test trying to read the lock
file while its exclusive handle forbade all sharing; verification moved after
handle release. Final host results above supersede that failed run. Local final
JUnit and LastTest.log are under the two test build directories. Build outputs
are ignored, not committed. No hardware was opened by tests.

Coverage includes setup-issued enable, selected DevicePaired admission, registry
continuity/Identify without reopen, auto-disable, timeout/cancel/quit/exception,
uncertain enable, bounded disable retries/failure reporting, transport recovery,
retired/out-of-window events and interactive continuation. Internal store tests
cover corruption/duplicates/unknown fields/stale writers/replacement failure;
separate test-owned processes prove concurrent rejection and OS lock release after
normal exit and forced termination. CLI checks test nonzero syntax/input/save
failures and zero successful scripts. Identify tests cover injected/default policy,
noise/outlier/stale/ambiguity/invalid quaternion/loss/recovery and evaluation metrics.
Existing protocol, runtime policy, Observation, service, UDP and C++/Java regressions
passed. None of these results prove hardware Pairing or physical Identify thresholds.

## Hardware checklist - all NOT RUN

1. Setup launch with stopped service/Hub.
2. Start pair in the console.
3. Put tracker into physical pairing mode.
4. Detect DevicePaired.
5. Confirm PairDevice disable.
6. List device in the same session.
7. Identify and human confirmation.
8. Tracker power cycle.
9. Transport reconnect.
10. Multiple trackers / selector behavior.
11. Pair timeout.
12. Pair cancel.
13. Cleanup-failure display and recovery.
14. Stationary jitter metrics.
15. Slow movement metrics.
16. Normal hand movement metrics.

Verified per-tracker identity provider and upper body-assignment/apply contract
are **implementation blockers**, not merely unexecuted hardware tests.

## Readiness and merge assessment

| Area | Decision |
| --- | --- |
| A. Pairing UX implementation readiness | Software ready; fake-HID exits/recovery pass. Physical Pairing remains HIL_REQUIRED. |
| B. Setup/Identify foundation merge readiness | Independently reviewable/mergeable as bring-up software foundation, subject to CI/reviewer acceptance. Policy defaults remain provisional and final live UX is not delivered. |
| C. Persistent assignment readiness | BLOCKED: no verified production identity provider or authoritative upper contract/apply API. Internal experiment is not completion. |
| D. Full config-free Altra workflow readiness | NOT READY: C plus service-owned setup/control integration and physical evaluation remain outstanding. |

D does not block an isolated A/B foundation merge. No merge or hardware cutover is
performed by this task. Identify policy values and metrics are documented in
`altra-setup.md`; implementation commits and pushed HEAD are reported separately.

---

## Historical validation at 68af2d0 (2026-10-08)

The following is prior evidence; it describes the superseded experimental CLI
and role model, not the current production setup interface.

# Altra setup validation receipt

Date: 2026-10-08. Base: `e8ecd513391ab4f15af2941380631e7f05049bea`.
Branch: `feature/altra-pairing-assignment-ux`.
Scope: setup foundation with explicit unavailable stable-identity and upper role API gates.
Full requested workflow: **INCOMPLETE**. Hardware: **NOT RUN**.

## Environment

- Windows x64, Visual Studio 18 2026, MSVC 19.51.36260.0.
- Windows SDK 10.0.26100.0.
- CMake/CTest bundled with Visual Studio 18 Insiders.
- Python 3.13.15; JDK 17.0.20 explicitly supplied (default discovery found Java 8).
- Release build; new tests use throwing checks and remain active with NDEBUG.

## Executed checks

| Check | Result |
| --- | --- |
| Fixed v2 artifact/manifest/source/contract/content hashes | PASS at configure and CTest |
| Historical fixed v1 dependency hashes | PASS at configure and CTest |
| Windows full Release build | PASS; service/probe/pair/setup/codec/test targets |
| Full CTest | PASS 13/13, including new setup_assignment and setup_cli |
| Diagnostic-only Release build (`VUT_BUILD_OBSERVATION=OFF`) | PASS |
| Diagnostic-only CTest | PASS 7/7 |
| Release tooling Python tests | PASS 13/13 |
| New PowerShell launcher syntax | PASS via PowerShell parser; live launcher/HID not executed |
| Whitespace self-review (`git diff --check`) | PASS |
| Existing tracking implementation diff | Empty for backend, decoder, adapter, publisher, service and session coordinator |

Commands (CMake/CTest resolved to the installed Visual Studio executable paths):

```text
cmake -S . -B build/assignment-ux -A x64 -DBUILD_TESTING=ON -DVUT_BUILD_OBSERVATION=ON
cmake -S . -B build/assignment-ux -A x64 -DJava_JAVA_EXECUTABLE=<JDK17>/bin/java.exe -DJava_JAVAC_EXECUTABLE=<JDK17>/bin/javac.exe
cmake --build build/assignment-ux --config Release
ctest --test-dir build/assignment-ux -C Release --output-on-failure --output-junit results.xml
cmake -S . -B build/assignment-ux-diagnostic -A x64 -DVUT_BUILD_OBSERVATION=OFF -DBUILD_TESTING=ON
cmake --build build/assignment-ux-diagnostic --config Release
ctest --test-dir build/assignment-ux-diagnostic -C Release --output-on-failure --output-junit results.xml
python scripts/test_release_v2.py
git diff --check
```

The initial sandbox CTest run failed assignment temp-directory writes and loopback UDP.
Those failures are recorded in the conversation and were not counted as PASS. Reviewed
host execution subsequently passed all tests. Final JUnit/CTest logs are local under
`build/assignment-ux/results.xml`, `build/assignment-ux/Testing/Temporary/LastTest.log`
and the corresponding diagnostic build paths. Build directories are ignored.

## New test interpretation

Pure assignment/persistence tests prove stable-key maps, assign/reassign/unassign/swap/
replace/clear, duplicate and unknown-role rejection, JSON validation and unknown-field
retention, stale writer/lock checks, and Windows atomic replacement failure preserving
the original. Event-level tests prove injected serial identity restore independent of
slot, RF address, session and discovery order, disconnected retention, replacement
isolation, and TrackingLost/recovery. Fake Windows HID tests prove the backend event
path can feed the registry across pair/unpair and transport reconnect.

Motion tests cover multiple samples, freshness, subthreshold noise, a single outlier,
duplicate RF packets, unusable tracking state, two moving candidates, post-localization
rotation-only identification and invalid quaternion rejection. No motion result assigns
a role or fabricates identity. CLI tests edit mock saved serial records offline; they
do not claim serial discovery from actual devices.

Existing Observation, RF admission, service session/config, launcher guard, UDP and
C++/Java cross-language tests passed. Pose/protocol/fallback/filter/coordinate behavior
was not changed. Passing mocks do not prove unchanged hardware performance.

## Unexecuted / unresolved

All requested physical tests remain **NOT RUN**: one/multiple discovery, physical
Identify, assignment, Bridge restart, tracker power cycle, enumeration change,
simultaneous reconnect, replacement, duplicate-role interaction, TrackingLost/recovery,
and completing setup without config editing.

Production hardware serial/stable UUID discovery is **NOT IMPLEMENTED**. The current
RF address is explicitly unsuitable as the persistent key. Upper consumer body-role
application/immediate reflection is **NOT IMPLEMENTED**. This branch contains no
changes to MonakaProtocol or MonakaBridge and no fake role output. It is not ready to
merge as completion of the full requested task. See `altra-setup.md` for the two APIs
needed to connect the implemented foundation to live hardware and consumers.
