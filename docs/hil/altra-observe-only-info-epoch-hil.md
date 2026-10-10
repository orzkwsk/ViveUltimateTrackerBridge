# Altra Stage 1 passive ObserveOnly info-epoch HIL

Source of Truth: the user-supplied
`MonakaVR_Altra_Stage1_ObserveOnly_Passive_HIL_Execution_Codex指示書.md`
for the execution receipt below; the original runner/software checkpoint used
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

Execution date: 2026-10-10 JST.
Branch: `hil/altra-observe-only-info-epoch`.
Required, local and origin HEAD at the execution gate:
`a52df6c916021dc16d081ff8a0409778ca8af939`.
Prepared lineage and clean worktree: verified before hardware access.
Existing Codex attachment files were preserved through a local Git exclusion;
no tracked configuration, implementation or contract file changed.

**Hardware result: `OWNERSHIP_OR_CAPTURE_AMBIGUOUS` — STOP at exclusive open.**

The operator confirmed A OFF, B OFF, both charging USB cables disconnected,
official VUT dongle connected, and VIVE Hub normally exited. Read-only inventory
confirmed the target dongle/HID nodes, no VIVE runtime process, stopped VIVE
Streaming Service, and no existing probe or capture actor. SteamVR and Virtual
Desktop processes remained present. A normal close request to SteamVR was
accepted, but subsequent unsandboxed inventory confirmed that it had not exited.
The initial sandboxed process check did not establish shutdown. No force kill,
VIVE service change or recovery restart was performed. The operator subsequently
authorized continuation with the inventoried dongle; its presence did not prove
sole HID ownership. The competing owner was not conclusively attributed.

All five current USBPcap extcap configurations were inventoried. A zero-access
root metadata query matched the current PnP parent to one interface, whose
configuration contained one direct device. Exact interface, topology, device
address and preference keys remain private. The capture selected that device
only, disabled capture-all/new-device options, and enabled descriptor injection.
TShark/Wireshark 4.6.6 USBPcap extcap created a fresh private pcapng. Before probe
launch, the pcapng header and target VID/PID descriptor were verified, with three
real interrupt transfers carrying data. Injected descriptors were not counted
as target traffic.

The unchanged, hash-verified runner emitted:

```text
runner_started
exclusive_open_failed
backend_stopped
```

Probe exit code: 2. Exclusive open: **FAIL**. No successful ownership window.
B OFF baseline: NOT RUN. `b_on_requested`: NOT CREATED.
`b_on_confirmed`: NOT CREATED. B normal ON: NOT REQUESTED.
Maximum-90-second passive observation: NOT RUN.
DevicePaired / epoch / nine-token sequence / InfoComplete /
initializationEligible / poseObserved / final epoch reason: NOT RUN.
NADS equality / NASS equality against HIL-05 B, official Pair B and official
reconnect B: NOT RUN; no fresh burst was collected.

The runner's existing failed-start path normally stopped the backend immediately;
there was no probe ownership to retain for capture cleanup. TShark was then
stopped with Ctrl-C. The capture launcher returned 1 on interruption. Final
unsandboxed inventory found no probe, TShark, dumpcap or USBPcap capture process.
The observation-result/capture-stop/audit/probe-stop sequence was not entered
because exclusive open failed before observation.

Capture file: private pcapng; exact path retained privately.
SHA256: `6be9d6e421293f42a6462322015e08a165b590ca21c007d62cfe9e618027c1ca`.
Final capinfos and offline TShark parse: **PASS**, 22 target frames,
zero truncated frames. Full-capture host vendor transmissions, including
HID SetFeature and data-bearing nonstandard host-to-device transfers: **0**.
Prohibited action list observed on wire: empty (PairDevice, PWS/behavior1,
behavior2/6, 0x18 TX, NA, FW1/FW2, ATM*, P63).
The probe start/exit interval contained zero captured frames and zero observed
host vendor transmissions. Automatic vendor action attribution under exclusive
probe ownership: **NOT ESTABLISHED**. A zero wire count alone does not satisfy
the hardware ownership gate or establish a passive reacquisition result.

Software regression rerun on the required HEAD: Windows x64 Release build
**PASS**; CTest **20/20 PASS**; release tooling **13/13 PASS** in normal and
optimized Python; optimized hardware-free runner CLI checks **PASS**.
The runner executable SHA256 remained unchanged from the software checkpoint.
The only public change for this execution is this sanitized report.
Execution leakage scan: **PASS**, checked against 17 private identity/topology/path
representations. Raw evidence remained Git-ignored, historical reference hashes
and the finalized capture hash were unchanged, and `git diff --check` passed.

Raw pcapng/CSV, exact extcap configuration, root/PnP/process inventory, runner
JSONL, operator receipts, capture hash/audit receipt and absolute timing remain
gitignored/private. Historical raw evidence was not modified. No pairing,
Unpair, vendor command, recovery action, dongle replug or Stage 2 was attempted.

Integration merge: **NONE**. Stage 1 merge: **NONE**.
Next gate: separately establish sole HID ownership, then obtain fresh physical
preflight confirmation before any newly authorized passive HIL attempt.
No acquisition or collector software failure is inferred from this startup stop.

## HID ownership forensic follow-up

Additional Source of Truth:
`MonakaVR_Altra_HID_Ownership_Forensic_Additional_Codex指示書.md`.
Execution date: 2026-10-10 JST.
Branch: `hil/altra-observe-only-info-epoch`.
Start local/origin HEAD: `9074d9314163d38c39bc9428ac7555959aa277f1`.
Clean worktree and the unchanged runner executable SHA256 were verified.
The last human-confirmed physical state was maintained: A/B OFF, both charging
USB cables disconnected, official dongle connected. B ON was never requested.
Current dongle presence, identity and PnP parent were rechecked privately.

**Final classification: `STEAMVR_OWNERSHIP_INVOLVEMENT_CONFIRMED`.**
The existing SteamVR-ON exclusive failure was reused without another identical
failure attempt. After verified normal SteamVR shutdown, the same binary opened
the dongle exclusively while Virtual Desktop remained running. This confirms
SteamVR involvement under the specified comparison conditions; it does not
identify a particular process/module as the handle owner.

### Phase A: SteamVR ON evidence

Read-only inventory captured vrserver, vrmonitor, vrcompositor, vrdashboard,
Steam and Virtual Desktop processes. No VIVE runtime process, probe or competing
capture actor was present; VIVE Streaming Service was stopped. Private receipts
retain process names, parent relationships, start times, session IDs, executable
paths and command lines.

Loaded modules were readable for vrserver (166 modules), vrmonitor (145), and
vrcompositor (87). Relevant vrserver modules included
`driver_vive_ultimate_tracker.dll`, `driver_htc_business_streaming.dll`,
`driver_VirtualDesktop.dll`, `driver_lighthouse.dll`, and `openvr_api.dll`.
vrmonitor also loaded `vivelinklib.dll` and `openvr_api.dll`; vrcompositor loaded
`openvr_api.dll`. Module presence is supporting evidence, not handle attribution.

The running Steam/SteamVR executable paths established the actual log location.
Current vrserver, vrmonitor, vrcompositor and vrdashboard logs were snapshotted
read-only and searched for VIVE/HTC/Ultimate/tracker/0bb4/0350/HID/driver/server.
Raw log bytes and matching lines remain private. ASCII-term summaries use
replacement decoding where the raw log encoding is mixed; originals are exact
byte snapshots. Current-run timestamps were correlated with each process start.
The server log contained 217 VUT-specific matches, including driver-load and
device-related evidence, from +1.165 s through +37121.041 s relative to server
start. The compositor log contained one current-run VUT-specific match. No
literal target VID/PID match was found in these current-run logs. Neither log
presence nor absence establishes the exact HID handle owner.

No existing handle inspection executable was found in PATH or the searched
installed-tool/download locations. Direct handle-owner inspection was therefore
NOT RUN. This does not establish that no owner existed. No diagnostic tool was
installed, no driver/registry/service configuration was changed, and no debugger
was attached or OS privilege elevation performed. Inventory used the existing
user's normal access outside the restricted execution sandbox.

The current extcap settings and zero-access root/PnP mapping were re-inventoried
before an eight-second, target-only SteamVR-ON passive USBPcap snapshot. Its
duration limit ended TShark normally with exit 0. Valid pcapng and target VID/PID
were confirmed: eight target frames, one real interrupt-data frame, zero
truncated frames, zero HID SetFeature and zero host vendor transmissions.
Known host vendor commands: none observed. Standard USB control/descriptor
traffic was excluded from vendor-action accounting. This snapshot provides no
process attribution or tracker reacquisition evidence.

### Phase B: normal shutdown and exclusive retry

The installed SteamVR `vrstartup.exe -shutdown` normal-shutdown function returned
0. Unsandboxed read-only inventory verified disappearance of vrserver,
vrmonitor, vrcompositor and vrdashboard. Steam could remain running. Virtual
Desktop Service, Streamer and Server remained present. No force kill was used.
The active VRChat process also exited during SteamVR shutdown; this affected
the active VR session, not merely the monitor window.

On the operator's subsequent steering, the first SteamVR-OFF capture was stopped
before any probe launch and all evidence was retained. After explicit instruction
to continue as planned, a new capture and new private probe paths were used.
The interrupted capture contained six target descriptor/control frames and no
vendor transmission. It was not an exclusive-open attempt.

SteamVR OFF / VD ON / VIVE runtime OFF, the same connected dongle, clean worktree
and runner hash were reverified. The fresh capture's pcapng header and target
descriptor were checked before ownership-only retry #1. No B-ON control file
was created. The runner emitted `runner_started` and
`exclusive_open_succeeded`; successful-ownership inventory confirmed one probe,
no relevant SteamVR process and Virtual Desktop still running.

With the probe still holding exclusive ownership, capture was stopped, hashed
and audited offline. The finalized retry capture parsed successfully: 52 target
frames, six interrupt-data frames, zero truncated frames, zero HID SetFeature
and zero host vendor transmissions. Known/prohibited host vendor action list:
empty. The full-capture count covers the exclusive-open window.

Only after the capture audit completed was the new stop file created. The runner
then emitted `backend_stopped`; final inventory found no probe or capture actor
and all four relevant SteamVR processes remained absent. The runner returned 2
because stopping an ownership-only run without B confirmation uses its existing
fail-closed abort classification (`OWNERSHIP_OR_CAPTURE_AMBIGUOUS`). This
acquisition-run abort receipt does not negate `exclusive_open_succeeded` or
the ownership comparison. No fresh-burst observation was armed or completed.

### Ownership matrix and scope

| State | SteamVR | VD | VIVE runtime | exclusive open |
|---|---|---|---|---|
| Initial, reused from the preceding HIL | ON | ON | OFF | FAIL |
| After verified normal SteamVR stop | OFF | ON | OFF | PASS |
| After VD stop | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

VD shutdown and retry #2: NOT RUN; retry #1 succeeded. VD remained running at
final inventory. B ON, pairing, recovery, dongle replug and fresh-burst HIL:
NOT RUN. Specific process/module handle owner: NOT ESTABLISHED.

Code, backend, protocol and configuration changes: **0**. Only this sanitized
report is published. Raw captures/CSV, logs, modules, inventories, exact extcap
settings, topology, hashes and operator/runner receipts remain Git-ignored and
private. Report validation includes evidence/receipt reconciliation, unchanged
runner/capture hashes, public leakage scanning and `git diff --check`.
Evidence reconciliation and capture-before-probe shutdown order: **PASS**.
Leakage scan: **PASS**, against 19 private representations. Optimized Python
hardware-free runner CLI/receipt/sender-boundary checks were rerun: **PASS**.
The prior Windows build/20-test regression checkpoint remains applicable to the
unchanged binary; no new implementation or build was required for this report.

Stage 1/integration merge: **NONE**. Next gate: a separately authorized fresh
physical-preflight run for ObserveOnly B-OFF baseline, normal B ON and passive
fresh-burst observation. This ownership task ends after evidence preservation
and normal capture/probe cleanup; it does not automatically start that HIL.
