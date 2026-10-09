# Altra Stage 1 passive ObserveOnly info-epoch HIL

Source of Truth: the user-supplied
`MonakaVR_Altra_Stage1_ObserveOnly_Passive_HIL_Codex指示書.md`.
Base: `fix/altra-info-epoch-gate`, `c17b244de64cab728086dafb5bd8acf6d711d6f8`.
HIL branch: `hil/altra-observe-only-info-epoch`. No Stage 1 or integration merge.
Research checkpoint `4560a086541b32102a0a0e962a68a89eada54d63` is read-only.

## Runner and software gate

`vut_epoch_probe` uses a fresh Backend instance with fixed ObserveOnly,
exclusive HID, configure-on-pair false and diagnostic SLAM queries false.
There is no policy override, explicit pairing call, raw vendor sender or
identity formatter. Backend/protocol/shared-contract/IPC code changes: **0**.
New vendor commands, changed payloads, default-policy and stable-identity changes: **0**.

Both stdout and optional JSONL serialize only a field allowlist: relative
monotonic time/sequence, kind, generation, epoch, slot, phase, progress,
seen mask, active, InfoComplete, initializationEligible, poseObserved,
enum reason and ACK label. All nine info labels are emitted without suffixes;
other ACKs become ACK_OTHER or ACK_UNKNOWN_N. DeviceId, RF, raw identity,
diagnostic/error text, paths and pose numbers never enter runtime output.
Epochs are emitted only on changed snapshots; pose packets do not flood logs.

Live use requires new private control/stop paths and starts with a >=5-second
B-OFF baseline. Write `b_on_requested` before requesting normal B ON, then
`b_on_confirmed` after the human reply. The confirmation starts a configurable
1..90-second observation (default 90); association between request and reply
is eligible, association before the request is ambiguous. Missing confirmation
ends ambiguously after 15 minutes. Control updates must be atomic and ASCII.
Stale signals/receipt paths and colliding control paths are rejected before open.

The runner latches collector violation and freezes polling immediately on a
terminal result. Normal deadline classification uses the current active epoch,
not a completion previously lost to unpair. It then holds ownership while the
capture stops; creating the stop file performs normal backend shutdown.
Cleanup wait is bounded to two minutes. Stop-file/SIGINT aborts fail closed.
The runner's receive classification alone is **not hardware PASS**: the private
capture audit and sole-owner evidence are required separately. Final shutdown
epoch invalidation is cleanup evidence, not a rewrite of the pre-stop result.

Clean Windows x64 Release build directory: `build/epoch-hil-clean`.
Visual Studio 18 2026, MSVC 19.51.36260.0, SDK 10.0.26100.0,
Python 3.13.15, JDK 17.0.20. Configure verified both fixed protocol kits.

```powershell
cmake -S . -B build/epoch-hil-clean -G "Visual Studio 18 2026" -A x64 `
  -DBUILD_TESTING=ON -DVUT_BUILD_OBSERVATION=ON `
  "-DJava_JAVA_EXECUTABLE=C:/Program Files/Microsoft/jdk-17.0.20.101-hotspot/bin/java.exe" `
  "-DJava_JAVAC_EXECUTABLE=C:/Program Files/Microsoft/jdk-17.0.20.101-hotspot/bin/javac.exe"
cmake --build build/epoch-hil-clean --config Release --parallel
ctest --test-dir build/epoch-hil-clean -C Release --output-on-failure --no-tests=error --output-junit epoch-hil-results.xml
python scripts/test_release_v2.py
python -O scripts/test_release_v2.py
vut_epoch_probe --help
python -O tests/epoch_probe_cli.py <built-probe> <built-offline-test>
```

Clean configure/build: **PASS**. Full CTest: **20/20 PASS**.
Release tooling: **13/13 PASS** each in normal and optimized mode.
Runner help and offline tests: **PASS**, including JSON parsing/field allowlist,
raw DeviceId/NADS/NASS redaction, fixed policy/exclusive open, no explicit sender,
changed-epoch formatting, unchanged-pose suppression, timeout boundaries,
no association, incomplete burst, early pose, duplicate/reorder/malformed/unknown
tokens, pre-request association, multiple slots, unpair and ownership/read failure.
Fake transport feature writes/GetFeature calls: **0** through start/poll/stop.
Release checks use explicit checks and remain active with NDEBUG and Python -O.
Public leakage scan: **PASS**, six changed public files against 15 actual
private identity/topology representations. Read-only reference-file hashes
were unchanged before/after. Test address/payload literals are synthetic.

The first sandboxed MSBuild did not progress and was cancelled; normal-environment
build completed. The first CLI run encountered a sandbox temporary-directory
permission restriction; its temporary directory is now under the build tree.
All final tests above passed after that repair. No live HID was opened by tests.

Runner executable SHA256:
`9035e23915eb91a54ac36696ecee5711bd3a7352bee0c4062f458a2d74907338`.

## Hardware receipt

Hardware gate: **NOT RUN — awaiting physical preconditions and ownership release**.
No tracker operation or capture has been started at this checkpoint.
Private process preflight found VIVE runtime still present and no competing
Bridge/capture actor. Normal UI exit was attempted; input failed with
`coordinate input geometry is unavailable`. No force kill or service operation.

Capture method/SHA256/attribution: NOT RUN.
A/B initial state and charging USB: awaiting operator confirmation.
B normal ON: NOT REQUESTED. Bounded window: NOT RUN.
Fresh DevicePaired / connection epoch / nine-token order: NOT RUN.
InfoComplete / initializationEligible / poseObserved / epoch reason: NOT RUN.
Automatic vendor actions on hardware: NOT MEASURED.
NADS/NASS byte-exact equality: NOT RUN.
Final hardware classification: pending; no hardware PASS/FAIL inferred.

All new raw evidence, exact extcap configuration, process inventory, operator
receipts, absolute timestamps, identity bytes and captures remain gitignored/private.
Existing historical private evidence is never modified. No extra pairing,
Unpair, recovery command, dongle replug or Stage 2 follows this gate.
