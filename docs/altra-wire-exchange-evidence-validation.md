# Altra Stage 0: wire exchange evidence validation

Stage 0 result: **PASS (software / fake transport)**. Hardware HIL: **NOT RUN**.

Base: `refactor/monaka-layer-separation` at
`a594e4377ef2b5cca04e915187b0c12e59064839`.
Implementation branch: `fix/altra-wire-exchange-evidence`.
Core implementation commit: `186af74`.
Research evidence `4560a086541b32102a0a0e962a68a89eada54d63` was read-only;
no research merge/cherry-pick or research report copy was performed.

## Exchange model and semantic boundary

`FeatureExchangeEvidence` exposes metadata, command ID, transport generation,
attempt/category counters, most recent response observation, framing reason and
exchange error. It contains no raw response, RF, serial or USB topology.

| Axis | States / definition |
|---|---|
| Write | NotAttempted, Failed, AcceptedByTransportApi, OutcomeUnknown (a write call threw and may already have written) |
| Response | NotObserved, Malformed, WrongCommandOnly, Matched |
| SemanticEffect | Unverified, ConfirmedByIndependentEvidence, RejectedByIndependentEvidence |
| Cleanup exchange | NotNeeded, Matched, Failed, Unknown |

Command exchange effects always remain **Unverified**. GetFeature matching
means a well-formed response with the requested command ID, not a tracker ACK,
RF behavior application, successful pairing or Ready/Tracking.

Response validation requires at least five HIDAPI bytes, declared length >=4,
and `declared + 1 <= actual buffer size` (the leading report ID is outside the
vendor declared length). Extraction is exactly `declared - 4` payload bytes
from offset 5; silent clipping was removed. Empty, short, unrelated, matching
malformed and matching valid responses are distinguished. The ten-attempt
bound remains. Wrong/empty responses may precede a valid match; anomaly counts
and the last observation are retained. Matching invalid declarations fail
immediately. Without a valid match, malformed evidence takes precedence over
unrelated-only evidence, then no response; the counters preserve mixed cases.

`sendAck()` now returns the actual host exchange match result and records
failures. `lastFailedFeatureExchange()` retains a failure even when later
configureOnPair sends match. The original bring-up sequence continues in its
original order; a failure does not introduce a different initialization or
recovery sequence. `lastError()` is specific and sticky until normal startup
clears it. Feature transport exceptions retain typed evidence and are rethrown
as sanitized `std::runtime_error`; transport exception text is not forwarded.

## Pairing and cleanup truth

PairingController still uses Idle, Enabling, Pairing, Paired, Cancelling,
TimedOut, Failed and CleanupFailed. Enable enters Pairing only after an accepted
write and well-formed matching exchange. `enableExchange` remains Unverified.
Only a selector-, time- and generation-bounded DevicePaired input sets
`associationEffect=ConfirmedByIndependentEvidence` and records `paired`.
Paired means observed association, with no Connected/Ready/Tracking inference.
A successful exchange without DevicePaired times out.

Backend sets cleanup eligibility before attempting enable. Explicit rejected
or unbuilt writes retain the base's no-accepted-enable/NotNeeded handling; this
does not prove that the vendor is off. Write-call exceptions retain
OutcomeUnknown, and response-call exceptions retain AcceptedByTransportApi;
both keep cleanup eligible. Missing/malformed/unrelated disable responses and
disable write failures keep pending cleanup. Close success never clears it.

`PairingCleanupEvidence` records enable generation and the actual disable
exchange generation. Matched confirms only that host exchange. Its semantic
effect remains Unverified even for Matched or NotNeeded. Official disable
effects have no independent confirmation in the referenced capture.

The compatibility bool `cleanupConfirmed` means only NotNeeded or a matching
disable exchange on the operation's generation. A reopened Backend can match
a new disable exchange and release its current pending-operation eligibility,
as in the base implementation. That new exchange does not confirm the old
controller's cleanup: its result remains CleanupFailed/Unknown, effect
Unverified. An old controller cannot send cleanup onto a different generation.
Same-generation cleanup observed before a transport close can legitimately
remain Matched, while its vendor effect is still Unverified.

Cancel/timeout/shutdown use the original two bounded cleanup attempts;
Backend::stop retains its final one-attempt cleanup-before-close path. Shutdown
and already-completed cleanup are idempotent. Stop is noexcept, keeps failed
exchange evidence through close/state clearing and contains close exceptions.
Controller result metadata is an operation snapshot; later best-effort Backend
cleanup remains independently available through the Backend accessor.

## Public API and callers

No existing bool API or core start/poll/snapshot/callback signature was removed.
`stop()` now declares its existing destructor-safe intent as noexcept.
Read-only accessors added: `lastFeatureExchange()`,
`lastFailedFeatureExchange()`, `pairingEnableExchange()`,
`pairingCleanupEvidence()`. PairingResult gains enable, cleanup and association
evidence; its legacy bool remains with a restricted definition. Rebuild header
consumers because result layout grows; all repository targets were rebuilt.

All callers were searched. Production control callers are PairingController,
SetupSession/SetupConsole and `vut_pair`; existing fake tests cover direct Backend
bool usage. CLI commands, paired fields and exit codes remain; diagnostic text
now says exchange matched/not needed or unresolved and reports vendor effect
Unverified. Existing exception handlers catch `std::exception` and remain valid.
No service IPC or service-owner boundary was changed.

## Build and tests

Clean build directory: `build/stage0-clean`. Windows x64, Visual Studio 18 2026,
MSVC 19.51.36260.0, Windows SDK 10.0.26100.0, Release. Configured with
BUILD_TESTING=ON and VUT_BUILD_OBSERVATION=ON; fixed v2 artifact/manifest/content
and historical artifact checks PASS. JDK 17.0.20 was explicitly selected instead
of the system Java 8 so the existing cross-language test was included.

```powershell
cmake -S . -B build/stage0-clean -G "Visual Studio 18 2026" -A x64 `
  -DBUILD_TESTING=ON -DVUT_BUILD_OBSERVATION=ON `
  "-DJava_JAVA_EXECUTABLE=C:/Program Files/Microsoft/jdk-17.0.20.101-hotspot/bin/java.exe" `
  "-DJava_JAVAC_EXECUTABLE=C:/Program Files/Microsoft/jdk-17.0.20.101-hotspot/bin/javac.exe"
cmake --build build/stage0-clean --config Release --parallel
ctest --test-dir build/stage0-clean -C Release --output-on-failure --no-tests=error --output-junit stage0-results.xml
python scripts/test_release_v2.py
python -O scripts/test_release_v2.py
```

CMake/CTest were invoked from the installed Visual Studio bundled CMake bin.
Configure and full build PASS. **CTest 17/17 PASS**, including protocol,
backend_pairing, setup_pairing, new wire_exchange, CLI, launcher fixture,
observation, session policy, loopback, cross-language and configuration checks.
Release tooling **13/13 PASS** in each normal/optimized run. These tests inject
fake HID or use offline/check-session modes; no real HID open or runtime
hardware operation was performed.

New matrix covers write rejection/acceptance, ten empty/short/unrelated replies,
wrong→valid and mixed retries, exact minimum and payload-bearing matches,
lower/oversized/truncated declarations, enable/disable exceptions after write,
no-event timeout, bounded association, cancel/shutdown/destructor, failed
cleanup retry/idempotency, close exceptions, transport loss and generation
change. A deliberately corrupted expected wire fixture was run using
`vut_wire_exchange_tests --corrupt-wire-fixture`; it failed with exit 1 at the
literal-byte equality check, proving Release checks are active.

## Base diff / wire gate

| Gate | Result |
|---|---:|
| New vendor command | 0 |
| Changed vendor payload | 0 |
| New hardware recovery behavior | 0 |
| New identity behavior | 0 |
| New service IPC behavior | 0 |

The protocol source/header and vendor types remain byte-identical to the base
(checkout newline normalization only). Windows HID/SetupAPI, configureOnPair,
PWS implementation, PairDevice builder arguments and all incoming dispatch
including NAZZ→FW1 are unchanged. The direct command sites still use the same
command IDs and payload builders. Tests compare complete 65-byte packets to
independent literal baseline fixtures for Pair enable/disable, host/client
initialization, kind04 TX, FW1 and slot-local PWS, including their order.

Malformed exchanges formerly misreported as success now take the existing
failure/cleanup paths. No retry budget, recovery command, profile, firmware/mode
command, info collector, stable identity or shared protocol was added. Stale
controller cleanup is prevented across generation changes; it does not become
a new recovery policy. No behavior2/6, kind03 sender, query1e01, FW2 or ATM21.

## Changed files and remaining work

- `include/vut/command_exchange.hpp`: typed evidence and cleanup definitions.
- `include/vut/backend.hpp`, `include/vut/backend_transport.hpp`: metadata accessors and evidence/API semantics.
- `src/backend.cpp`: framing/error propagation, enable/disable truth and noexcept stop.
- `include/vut/pairing.hpp`, `src/pairing.cpp`: operation snapshots, bounded association and generation-safe cleanup truth.
- `src/setup_console.cpp`, `tools/vut_pair.cpp`: exchange wording and unverified cleanup effect.
- `tests/wire_exchange_tests.cpp`, `tests/backend_pairing_tests.cpp`, `tests/setup_pairing_tests.cpp`, `CMakeLists.txt`: fake matrix, literal-byte regression and corrected old-generation expectations.
- This validation note.

Remaining P0: **connection epoch / info collector**, proposed next branch
`fix/altra-info-epoch-gate`, not implemented here. Immediate configureOnPair and
unconditional NAZZ→FW1 remain deliberately unchanged. Stage 0 is review-ready
for its software scope; it makes no new hardware safety or readiness claim.
No merge into refactor or research is performed by this task.
