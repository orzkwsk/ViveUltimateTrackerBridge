# Altra Q1E-only single-B mapping discovery HIL

Source of Truth: operator-supplied
`MonakaVR_Altra_Q1E_Only_Mapping_Discovery_HIL_Codex指示書.md` and the request's
independent `ACTIVE HIL GATE Q1E-ONLY` approval. Exact base:
`0c013c1e4ac7c2e3c2340c56e589311b9553380d`.
Branch: `hil/altra-q1e-mapping-discovery-b-single`.

The separate default-OFF `vut_q1e_mapping_hil` research executable reuses the
qualified exclusive HID transport and durable journals. It has no Behavior2
sender or dependency on the prior active session. Its only feature write uses
the unchanged offline-qualified `buildObservedQuery1eReport()`; exactly one
GetFeature follows an accepted write. No retry, replay or automatic vendor action
exists. A shared persistent trial journal and per-run gate intent consume the
single trial before API access, including failure/exception cases.

The targetless host-to-dongle request requires no current slot/RF/address binding.
Physical B-only confirmation, current exclusive transport generation, ten-second
B OFF and thirty-second confirmed B ON baselines, healthy recent capture proof
with zero vendor transmissions, independent human approval and a current one-shot
run/token remain mandatory. A token alone is not human approval. A second tracker,
mutation, registration-loss evidence, unqualified framing or transport loss latches
STOP. Natural fresh connection/info/pose prevents a send. Ordinary successful or
empty-response cases observe at least thirty seconds after API return; unsafe
conditions stop immediately. Response records remain opaque and never create
identity, slot/RF mapping or readiness.

Production sources/APIs/shared contract are unchanged. Research CMake targets
default OFF and are excluded from release packaging. A common HID interface
selection helper was extracted without changing the previously qualified filter.

Software qualification: **SOFTWARE_QUALIFICATION_PASS**. Windows x64 Release
configure/build and full CTest **27/27 PASS**. Release tooling **13/13 PASS** both
normally and with `python -O`. Static reachability/no-retry/Behavior2 exclusion,
targetless eligibility, duplicate/stale/wrong approval rejection, persistent trial
lock rejection, minimum windows and natural-reconnect suppression PASS. Tests use
fake transports; CLI tests only execute help/rejected arguments/stale locks before
the hardware path. Default configure excludes both research executables; leakage,
original raw preservation and `git diff --check` PASS.

Frozen runner SHA256:
`53210bb952921975d550e653bd4eee9b5b116d742a8053aa7f595fc2e02edd08`.

## Hardware run: stopped before HID open

Run alias: `Q1E-CAPTURE-PREFLIGHT-01`, 2026-10-10 JST. Frozen source commit:
`fd238e5df3bb212c1d6e7abcf75c757014d01519`, with the runner hash above.
Fresh operator preflight confirmed A OFF, B OFF, both charging USB disconnected,
dongle connected and VIVE Hub closed through the normal user-operated procedure.
SteamVR/Virtual Desktop remained running. No app/process/service recovery was
performed. The four SteamVR processes and original Virtual Desktop processes
retained their IDs and creation times. The later inventory additionally contained
`VirtualDesktop.Server.exe`; this is an observed inventory change, with no agent
lifecycle operation or cause attribution. The complete inventories are private.

Read-only current PnP found one healthy target. Existing USBPcap metadata identified
one target root and one direct device for the target-only capture. TShark was
started with all-device/new-device capture disabled and descriptor injection
enabled. Its extcap input failed during startup:

```text
File type is neither a supported pcap nor pcapng format. (magic = 0x00000000)
```

TShark exited with a nonzero result; no raw pcapng file was created. This diagnostic
does not identify the failure's root cause. Capture target VID/PID and integrity
could not be verified from raw packets. The traffic check failed before the runner
launch, so no real HID open, SetFeature or GetFeature occurred. Previous HIL's
exclusive-open success was not reused as a current ownership receipt.

The run stopped under Source of Truth sections 15/29: healthy capture is required
before active HIL and unavailable/corrupt capture is a STOP condition. No capture
retry, new tool installation, driver/registry/service change, dongle replug or
application lifecycle operation was performed. Read-only final inventory confirmed
zero capture/probe actors. No runner ownership or active-trial journal was created.

| Required hardware gate | Actual result |
| --- | --- |
| Exclusive owner / transport generation | NOT RUN / not established |
| B OFF passive baseline | NOT RUN |
| B ON request and 30-second baseline | NOT RUN; B remained OFF per preflight |
| Current mapping / natural reconnect | NOT EVALUATED |
| Q1E-ONLY human approval | Not requested; none |
| Q1E sends / active trials | 0 / 0, maximum 1 each |
| Behavior2 sends | 0; inaccessible in this runner |
| Transport result / response framing | NOT RUN |
| Opaque record / RF transition | Not acquired / NOT EVALUATED |
| DeviceUnpaired / DevicePaired / NANA..NAZZ | NOT EVALUATED |
| Fresh NADS / NASS / pose / connection epoch | Not acquired / NOT EVALUATED |
| Post-Q1E 30-second observation | NOT RUN; no Q1E send |
| Automatic vendor action raw audit | NOT EVALUATED; capture unavailable |

Research hardware feature API calls were zero. This is not a raw-capture proof of
zero vendor actions by every system actor. No response-only, mapping-discovery,
identity stability/acquisition or reconnect effect is claimed. Private NADS/NASS
comparisons are NOT RUN because no fresh values were acquired.

Classification: **Q1E-F — capture infrastructure failed before HID access**.
Observed Q1E wire safety: **Unknown**. Mapping discovery utility: **Unknown**.
Device-info acquisition utility: **Unknown**. Reconnect utility: **Unknown**.
Vendor semantics remain **HIL_UNVERIFIED**. Hardware active send: **NOT RUN**.

Private evidence includes launch/configuration metadata, current PnP, process and
service inventories, frozen runner/source hashes, software qualification receipts,
operator preflight and the original tool diagnostic. No raw pcap, raw response,
opaque record or new identity value exists for this run. No private path, original
run ID, topology, process ID, address or token is published.

The run is closed. Capture infrastructure must be qualified in a separate follow-up
before a fresh hardware run; no retry or second trial is scheduled. Production
changes: none. Integration/production/previous-HIL merges: none. Only this branch
contains the isolated runner/tests, required CMake/reachability wiring and sanitized
report. Existing production/shared-contract/qualified primitive sources are unchanged.
