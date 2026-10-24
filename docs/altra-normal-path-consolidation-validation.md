# Altra normal-path production consolidation

Date: 2026-10-10 JST. Repository: `orzkwsk/ViveUltimateTrackerBridge`.
Branch: `feature/altra-normal-path-consolidation`.
Base: `56b9a269ea3f76068a7e69c54b108efa2c1556a8` (ServiceRuntime owner SoT).
Reference only: `fix/altra-info-epoch-gate`, `c17b244de64cab728086dafb5bd8acf6d711d6f8`.
Semantic sources: `186af74d8d7e9a338341a61c4f64c1ac0a75ac0c` and
`407a00adef307344820d87afb868952ea0660034`.
Supplied instruction SHA256:
`44c8b996f75bda46599802dace8db5a1e8b51e09da0e98fe300fb5cacc5036a7`.
Common prepared-lineage ancestor: `a594e4377ef2b5cca04e915187b0c12e59064839`.
The current refactor branch advanced along the reference side after the owner
branch fork; its present HEAD is not an ancestor of the requested base. The owner
receipt records the prepared refactor HEAD at this common ancestor. The requested
base is used exactly; no history is rewritten and no whole commit is merged or
cherry-picked. Existing worktree was clean; unrelated branches/worktrees are preserved.

## Read-only audit recorded before implementation

| Class | Files / semantics | Resolution |
| --- | --- | --- |
| A: portable | command exchange evidence; BackendTransport contract comments; pairing result/controller exchange versus association effect; setup console and pairing CLI wording; wire exchange and setup regression tests | Reapply reviewed semantic changes, preserving feature packet construction and explicit setup ownership. |
| B: lifecycle overlap | Backend start/stop/poll epoch updates; automatic vendor policy; connection/info epoch collector; service default and reconnect/shutdown evidence | Keep owner architecture; add runtime-owned provenance/registry-gated transient evidence and ObserveOnly default. Preserve stable callback and ordered teardown. |
| C: superseded | Reference service composition and standalone adapter/publisher ownership; reference CMake without control/runtime targets; source documentation as an implementation authority | Retain ServiceRuntime, DeviceRegistry, service control and existing targets. Summarize reference findings here. |
| D: research only | Q1E, Behavior2/RestartRf, PWS recovery experiments, automatic reconnect research, HIL runners, USBPcap tools, observed_reconnect_wire.hpp | Do not import or connect. No hardware access. |

Compared backend.hpp, backend_transport.hpp, backend.cpp, pairing.hpp/cpp,
setup_console.cpp, vut_pair.cpp, service runtime/control/registry, CMake and tests.
Backend/pairing files on the owner base match the common ancestor, so their narrow
reference changes do not conflict with owner code. Runtime default vendor sends
intentionally change to zero; explicit Legacy retains existing bytes/behavior.
Source epoch matching invalidated a replacement on a late old unpair; preserve the
owner rule that a late old unpair cannot retire the new occupant. Source tracker
also needs cross-slot ownership invalidation and explicit fresh NADS/NASS/candidate
metadata for the future provider. These will be reconciled without identity promotion.

## Ported semantics and conflict resolutions

- Feature exchange uses typed transport-write/framing/result metadata. Matching a
  feature response never confirms RF pairing/disable effect. Primary enable failure,
  cleanup exchange, association evidence and old-generation uncertainty remain separate.
- PairingController confirms association only from bounded independent DevicePaired
  evidence. Selector validation, timeout/cancel, ownership locks, offline failure,
  pairing lab gates and body-assignment separation remain intact.
- The original exact nine-token B profile collector is ported with fail-closed
  ordering/shape/duplicate handling; its existing two-trace scope is not generalized.
  No token contents or serials are retained, parsed as stable identity, or refreshed.
- Backend owns a mutex-protected collector for standalone pairing/setup/diagnostics.
  ServiceRuntime owns a separate instance of the same pure collector for production.
  This avoids a Backend call inside dispatch and binds evidence to runtime-accepted
  timestamp/generation plus registry ownership. Backend-only evidence is not the
  authoritative service identity input.
- Active snapshots expose transient deviceCandidate (RF/slot addressing), current
  connectionEpochId, transportGeneration, NADS/NASS present and fresh flags, progress,
  completeness, phase and invalidation reason. Invalidation clears candidate and
  both fresh flags; historical seen flags remain diagnostic only. Epoch IDs are not
  registry connection epochs, RF packet counters, wire sessions or persistent IDs.
- Late old unpair cannot invalidate the replacement or erase its Backend cache.
  Same-slot replacement starts empty; same-RF slot moves invalidate old evidence;
  ambiguous registry ownership invalidates all affected actionable evidence.
- ServiceRuntimeConfig defaults to ObserveOnly; BackendConfig keeps Legacy for
  existing explicit setup/probe compatibility. Production vut_service uses the
  runtime default. Shared-open is unchanged; active SLAM-query config remains rejected.
- ObserveOnly suppresses implicit cleanup on stop/loss/reopen. Pending explicit
  pairing cleanup blocks passive reopen without writing. Explicit endPairing and
  PairingController cleanup remain available; explicit Legacy reopen preserves its
  historical cleanup exchange behavior without promoting old-generation effects.
- The pre-existing vut_service automatic host reopen/retry loop was removed to
  satisfy this task's no-automatic-reconnect scope. It now surfaces unavailable and
  exits on initial open failure, transport loss or required session restart. Explicit
  runtime start/reopen and strict/preserve revision lifecycle gating remain tested.
  No active RF recovery primitive or reconnect research is adopted.

## Preserved owner and identity invariants

Receive order: Backend parse/standalone epoch -> stable runtime dispatcher provenance
gate -> runtime epoch -> DeviceRegistry/control -> five-entry ownership reconciliation
-> unchanged adapter admission -> publisher enqueue -> post-poll flush.
No Backend lifecycle/feature method, worker, filesystem/logging or publisher flush
was added to dispatch. It does not scan registry history to collect info tokens.
The stable callback, single owner/reentrancy guard, immutable detached snapshots,
transport/session/generation separation and shutdown/reconnect gates remain intact.
Epoch invalidation precedes runtime-owned Backend stop and dependent destruction;
the existing Backend read-failure close occurs before poll returns, as on the base.
Observation-session restart, runtime shutdown and faults invalidate evidence.
Max epoch storage is current + previous per each of five slots for each collector.
Registry history/capacity and 256-record fault behavior are unchanged.

No StableIdentity is emitted. Current unambiguous records remain
Unavailable/ProviderNotInstalled after a complete NADS/NASS burst. Existing retired
and ambiguous registry status truth is preserved; no Candidate/Verified promotion
or persistent assignment API is introduced. Upper-layer wire API, codec, imported
kit, adapter, publisher and coordinate mapping are unchanged.

## Policy and automatic vendor action matrices

| Context | Default / selection | Automatic sends |
| --- | --- | --- |
| Production service/runtime | ObserveOnly | 0 at startup, receive, stop, loss and explicit passive reopen |
| Explicit ObserveOnly Backend | Overrides configure and diagnostic-query flags | 0; pending cleanup blocks reopen |
| Standalone setup/pair/probe compatibility | Legacy Backend default | Existing configure/FW1/slot-local PWS and diagnostic opt-in semantics retained |
| Explicit begin/endPairing | User-owned setup operation | Existing enable/disable packet bytes; exchange evidence only |

| Automatic action | ObserveOnly production | Explicit Legacy compatibility |
| --- | --- | --- |
| PairDevice startup | 0 | 0 (pending old-operation disable exchange only) |
| Configure-on-pair | 0 | Existing byte sequence |
| NAZZ -> FW1 | 0 | Existing byte sequence |
| PWS on unpair | 0 | Existing slot-local byte sequence |
| SLAM diagnostic queries | 0 | Existing probe opt-in only; service rejects config |
| NA / Q1E / Behavior2 / RestartRf | 0 | 0 |
| Active recovery / automatic service reopen | 0 | No new recovery path; service exits unavailable |

Q1E/Behavior2/RestartRf, PWS recovery experiments, PairDevice startup, HIL runners,
USBPcap tools and observed_reconnect_wire.hpp were not imported. Research production
reachability is checked over C++ source/header closure, CMake, release packaging and
normal service/setup launcher files. Historical reference documents are retained as
history; their fixed-C1/old production authority is not reinstated.

## Validation status

Windows x64: Visual Studio 18 2026 / MSVC 19.51.36260.0 / Windows SDK
10.0.26100.0; Python 3.13.15; Java 17.0.20. CMake/CTest executables from
`C:/Program Files/Microsoft Visual Studio/18/Insiders/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/`.
Ignored evidence directory: `build/altra-normal-path-consolidation/`.

```text
cmake -S . -B build/altra-normal-path-consolidation -A x64 -DBUILD_TESTING=ON -DVUT_BUILD_OBSERVATION=ON -DJava_JAVA_EXECUTABLE="C:/Program Files/Microsoft/jdk-17.0.20.101-hotspot/bin/java.exe" -DJava_JAVAC_EXECUTABLE="C:/Program Files/Microsoft/jdk-17.0.20.101-hotspot/bin/javac.exe"
cmake --build build/altra-normal-path-consolidation --config Debug --parallel 6
ctest --test-dir build/altra-normal-path-consolidation -C Debug --output-on-failure --no-tests=error --output-junit debug-results.xml
cmake --build build/altra-normal-path-consolidation --config Release --parallel 6
ctest --test-dir build/altra-normal-path-consolidation -C Release --output-on-failure --no-tests=error --output-junit release-results.xml
python scripts/test_release_v2.py
python -O scripts/test_release_v2.py
python tests/normal_path_reachability.py
python -O tests/normal_path_reachability.py
build/altra-normal-path-consolidation/Release/vut_info_epoch_tests.exe --corrupt-epoch-fixture
build/altra-normal-path-consolidation/Release/vut_wire_exchange_tests.exe --corrupt-wire-fixture
git diff --check
```

| Check | Actual result |
| --- | --- |
| Configure / pinned v2 and historical v1 integrity | PASS |
| Windows Debug build | PASS, debug-build.log |
| Windows Release build | PASS, release-build.log |
| Full Debug CTest | PASS 22/22, 46.87 s, debug-results.xml / debug-ctest.log |
| Full Release CTest | PASS 22/22, 18.86 s, release-results.xml / release-ctest.log |
| Release tooling | PASS 13/13 |
| python -O release tooling | PASS 13/13 |
| Static reachability, normal and -O | PASS: 43 production C++ files, research recovery references 0; CMake/package/launchers audited |
| Release corrupt epoch and wire fixtures | Expected FAIL, exit 1 both; proves CHECKs execute under NDEBUG |
| Injected research include and Q1E CMake target in a temporary copy | Expected FAIL both; production files restored/preserved |
| git diff --check | PASS |

Release manifest now requires all 22 native/control/setup tests (plus its two
existing codec model tests when MONAKA_BUILD_TESTS is enabled), so missing new
boundaries cannot produce an accepted release receipt. Existing release-tool suite
was rerun normally and with -O after this manifest change. No release archive or
deployment is claimed by these tooling unit results.

The first sandbox MSBuild invocation stalled without reaching compilation; it was
cancelled, then the same workspace Debug/Release builds and complete CTests ran on
the host successfully. No software failure remains. The sandbox Python launcher
printed a location warning while continuing successfully; verified scripts and
test subprocesses returned zero. No new compiler warning/error was found in either
successful build log.

Coverage: owner/callback lifetime/reentrancy, receive/admission/publication order,
shutdown and exception teardown, explicit reconnect revision/session gating, default
256-record HistoryFull, synthetic publication flood and exact emitted Observation
bytes; complete/incomplete/duplicate/mutated/malformed info bursts, slot/RF changes,
stale generation, disconnect and runtime restart invalidation; passive evidence
before registry snapshot/admission and Unavailable identity after a full burst;
real Backend parsing through in-memory transport with ObserveOnly send count 0;
pending-cleanup passive reopen no-send; Legacy exact feature packet bytes including
setup and cleanup exchanges. Existing setup lab/lock/offline/body separation tests
and C++/Java cross-language tests passed in both configurations. All native new
checks use explicit CHECKs; new Python audit uses exceptions, not assert.

Pinned v2 source remains `572e58cfa20b8b4335207ea5dbcb3f04c587ddff`, kit SHA256
`a55567425d071e7338b89f4e6caa925676014fd356aa585730deb1f9d11cd7c3`.
No protocol-dependent migration or contract modification was performed.
Hardware/USB/dongle/tracker access: **NOT RUN** (not required).

## Delivery

Implementation commits on the requested base:

- `90212bf`: typed command exchanges, pairing effects/cleanup separation and UX compatibility.
- `8b7b3ec`: passive current epochs, runtime ownership reconciliation, ObserveOnly,
  unavailable service exit, cross-layer tests and release/reachability gates.

This report is committed separately. Push destination is only
`origin/feature/altra-normal-path-consolidation`; no integration/dev/main merge or
HIL/research merge is performed. Consolidation software gate: **PASS**. Production
cutover and identity-provider completion are not claimed; blockers below remain.

## Known remaining blockers

Stable identity provider; NADS/NASS promotion policy; persistent assignment;
service IPC/control API; exclusive production ownership; HistoryFull retention;
stuck-dongle recovery; automatic reconnect. Default 256-record exhaustion remains
a registry fault. Recovery is deferred. Next task: passive stable identity provider,
NADS primary / NASS cross-check / fresh current epoch only / fail-closed / no active refresh.
