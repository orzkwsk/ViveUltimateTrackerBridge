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
