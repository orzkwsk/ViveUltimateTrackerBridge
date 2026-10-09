# Altra control architecture: read-only reference audit

Audit date: 2026-10-09 JST. Primary checkpoint and task scope are in the
[architecture](altra-service-control-architecture.md). No other repository was changed,
committed, pushed or cherry-picked. Reference code was inspected through Git objects,
the supplied generated kit and GitHub GET tools, without executing vendor software,
debug consoles, launchers or HIL procedures. No third-party RE source was copied.

## Evidence vocabulary

| Label | Meaning |
| --- | --- |
| CODE | Actual source inspected at the stated immutable commit; existence/behavior of code, not hardware proof |
| SW-HIST | Committed software test receipt from a previous task; not a test executed by this audit |
| HIL-HIST | Historical hardware report, with conditions and missing trials preserved; this audit did not repeat or independently re-decode private raw captures |
| OFFLINE-HIST | Saved-capture analysis/report; no new device trial and no stability promotion |
| PUBLIC-CODE | Public SDK or RE text inspected at a fixed commit; no automatic vendor-contract adoption |
| PROPOSAL | New architecture choice/inference, requiring implementation/tests and any listed HIL |

Confidence is high for inspected code structure; it may still be low/unknown for physical
semantics. `reusable` means reuse existing owned components in a future scoped change;
`partially reusable` means preserve an idea/interface with required changes;
`historical only` preserves provenance; `unsafe / obsolete` rejects production adoption.
None of these labels authorizes copying/cherry-picking a reference implementation.

## Primary repository and historical branches

All rows in this table are `orzkwsk/ViveUltimateTrackerBridge`. Full commit IDs are
recorded so branch movement cannot silently change the evidence. Primary/research remote
HEADs were checked with `git ls-remote`; other branch IDs are fetched origin refs.

| Branch / commit | What was inspected | Reuse classification | Evidence / confidence / notes |
| --- | --- | --- | --- |
| **Primary** `refactor/monaka-layer-separation` / `a594e4377ef2b5cca04e915187b0c12e59064839` | `tools/vut_service.cpp`; Backend/transport; adapter/publisher/session; PairingController; SetupSession/Devices/SetupConsole; setup launcher; CMake/tests; rework/production docs | reusable Backend/decoder/adapter/publisher/session; partially reusable controller/registry/frontends | CODE high; single callback + main-thread polling; no production IPC. SW-HIST receipts are separate from current audit tests |
| `fix/altra-pairing-assignment-ux-rework` / `a594e4377ef2b5cca04e915187b0c12e59064839` | Same source checkpoint; `docs/altra-setup-rework-audit.md`, `docs/altra-setup-validation.md` | reusable boundary decisions; partially reusable setup model | CODE / SW-HIST high; same-session fake pairing and provisional Identify, no hardware readiness inferred |
| `feature/altra-pairing-assignment-ux` / `e2d0c01fe7633acb3f7fc8e628b2acad624a602a` (prior implementation `68af2d05ad8ef7cfceff624cff66c58569edd853`) | Commit/ref identity and retrospective audit/validation embedded at primary; role/store changes described there | historical only; obsolete local body-role ownership | SW-HIST / retrospective source report, medium; this audit does not claim a full independent code audit of that older feature tip |
| `research/altra-stable-identity-hil` / `d78dab2041266cd4cad462bf4784abc3131b556c` | `src/identity_capture.cpp`, capture header/procedure, backend diff, `docs/hil/altra-stable-identity-hil-results.md`, offline research report; sanitized sequences/tool descriptions | partially reusable exact-byte evidence/parser/fail-closed policy; historical only capture runner | CODE high; HIL-HIST + OFFLINE-HIST qualified; passive command guard absent from primary, no provider promotion |
| Same research branch / `d67ac295a29290de5186efead549e7829ff1df29` | Commit lineage/diff and final offline-analysis test/report receipt | partially reusable sequence/raw-provenance validation approach | OFFLINE-HIST; synthetic tests cannot qualify acquisition; no research code merged |
| `dev/vivehub-bypass` / `db7c6795055fc00054b43a64e3bbd658a3575932` | README and backend history/differences used to locate experimental lineage | historical only | CODE/document history high for boundary; README's implementation claims are not current HIL evidence |
| `experiment/vive-auto-reconnect` / `24228627f535c3fec253451a6598ef1540e26be5` | Backend command paths, `docs/experiments/vive-auto-reconnect-slot-specific-hil.md`; tree of runner/handoff docs | partially reusable experiment provenance/build guards; unsafe / obsolete startup/runtime RestartRf recovery policy | CODE + HIL-HIST; clean old cold-start-payload failure is recorded; slot-specific procedure is not proof of completed success |
| `experiment/vive-debug-console` / `1e92903673180fbf2d2964bf239dcf94dcbfcb53` | `tools/vut_debug_console.cpp`, `docs/experiments/vive-debug-console.md` | partially reusable enqueue-on-input / execute-after-poll idea; historical only PWS report; unsafe production raw/reboot/policy surface | CODE high; queue unbounded, detached blocking stdin thread, unlimited drain. HIL-HIST slot0 PWS transition is narrow, not multi-tracker/concurrent-pairing proof |
| `experiment/vive-test-harness` / `7ee46d6ca3e725d6c2d8dff8c996dbf70546a85c` | DebugConsole input/queue/raw-control sites; stale-binary evidence doc; tree/diffs | partially reusable dedicated build/hash provenance; unsafe production detached input/raw commands | CODE + HIL-HIST; `asda.pcapng` used wrong transmitted payload, so intended slot-specific implementation was NOT TESTED in that capture |
| `diagnostic/vive-slam-recovery-controls` / `c8a2030872edb3461a1e29a9915f277dad89a129` | Service/header diff and hotkey control execution after publisher flush | partially reusable manual service-owned control placement; unsafe production slot-targeted SLAM/reset/query surface | CODE high; no stable-handle/IPC authorization contract, no evidence of identity refresh or concurrency qualification |

Stable-identity historical limits (from the pinned research report): NADS/NASS both Class B.
Two physically labelled trackers differed; reversed connection order preserved values.
A later fresh A observation changed RF and slot with unchanged candidate bytes. B's
post-pair trial was interrupted, not completed. Restart alone, isolated power cycle and
dongle reopen lacked fresh identity ACKs in their recorded windows. Those passive trials
suppressed ordinary Backend feature exchanges, and other vendor services/SteamVR remained
present. They do not establish guaranteed normal-service acquisition or a fully Hub-free
runtime. First pose versus identity-burst ordering was not recorded. No private serial,
RF value, raw capture or Hub cache content is reproduced here.

## Cross-repository fixed references

| Repo / branch or pin / full commit | What was inspected | Reuse classification | Evidence / confidence / implications |
| --- | --- | --- | --- |
| `MonakaVR/MonakaProtocol` / generated wire-v2 source pin / `572e58cfa20b8b4335207ea5dbcb3f04c587ddff` | Supplied ZIP/manifest, bundled `docs/architecture-revision.md`, `docs/C2.md`, lock and existing integrity verifier | reusable **as fixed dependency**; not a new control schema | CODE/contract high; clean-source kit pin, independent control version; no shared wire edit |
| `MonakaVR/MonakaBridge` / `refactor/monaka-layer-separation` / `84f5b7529694d19aee98461580f3cfe628bf3cbc` (branch checked via GitHub) | `include/monaka_bridge/config.hpp` blob `0d69cc8c564092e95d4c90a07fab911f177024c2`; `src/mapping/config.cpp` blob `fed83a7c2ca6f2bec2df7cc9bd363b5e921b8fe8` | partially reusable responsibility/config transaction concepts; no VUT service API implementation to adopt | CODE high; source/device -> logical tracker/profile/spaces/transforms/routing, explicit migration. Inspected API is not a body-target/physical-serial assignment contract |
| `MonakaVR/MonakaVR` / `refactor/monaka-layer-separation` / `17c4c9bb712b425bd72911837c7df010dee5bd62` (branch checked via GitHub) | Complete recursive tree (not truncated); `TrackerBodyAssignments.kt` blob `e6e71605d413474089f824b944c4813db5ac9148`; `ObservationBackendRunner.kt` blob `f2ef1c467467753d1adf7fde0051a4a3d716e10d` | reusable upstream responsibility model; historical/reference only for VUT implementation | CODE high; upper assignment uses logical publisher/source/tracker and Main/Fallback constraints. No Altra/VIVE/VUT path name found in tree; inspected files do not supply a VUT serial-to-logical apply adapter |
| `ViveRoboticsSoftware/vive-ultimate-tracker-sdk` / immutable public research pin, branch HEAD not asserted / `8685112780734c2513c66d53b9eb104db47afcf9` | `include/ultimate_tracker.h` blob `007cdc44efc36b5d9461c9696c81d1469b4959dc`; `tracker_ipc_client_ref.py` blob `2ffeccdfc133fa3afc592ed9587f3adcad8d4ec8`; tree to distinguish source/API from packaged binaries | partially reusable service/client lifecycle idea; vendor wire/command set not reusable as our contract | PUBLIC-CODE high for declared interface; no execution/HIL. Linux Unix-domain IPC mixes pose/events/command status; tracker numeric index/name not proven stable identity. Python reference temporarily blocks on partial receive/send and is unsuitable for our HID executor. No bundled daemon/binary downloaded |
| `shinyquagsire23/vive_ultimate_tracker_re` / immutable public research pin, branch HEAD not asserted / `11ae3cbfd9a331694c72890df7dced77248b4ba6` | `tracker_toybox/enums_horusd_ack.py` blob `f6c6b8d6e3ceec8ccd1290f20f724f95796d9ee9`; recursive tree; research report's separately labelled prior sender audit | historical only namespace clues; unsafe to promote speculative commands | PUBLIC-CODE high that annotation is uncertain, low for physical command meaning. NADS/NASS naming suggests separate serial namespaces; NA resend comment is speculation. No query or new RE code copy |
| Microsoft Learn / Win32 documentation, accessed 2026-10-09, no Git commit applicable | Named Pipe security/access rights; CreateNamedPipe modes | reusable platform guidance | Official documentation, not an exercised IPC implementation or device evidence. Explicit DACL/logon policy, remote rejection and overlapped I/O inform the proposal |

Fixed links:

- [Protocol C2](https://github.com/MonakaVR/MonakaProtocol/blob/572e58cfa20b8b4335207ea5dbcb3f04c587ddff/docs/C2.md).
- [Bridge config API](https://github.com/MonakaVR/MonakaBridge/blob/84f5b7529694d19aee98461580f3cfe628bf3cbc/include/monaka_bridge/config.hpp)
  and [implementation](https://github.com/MonakaVR/MonakaBridge/blob/84f5b7529694d19aee98461580f3cfe628bf3cbc/src/mapping/config.cpp).
- [MonakaVR assignment](https://github.com/MonakaVR/MonakaVR/blob/17c4c9bb712b425bd72911837c7df010dee5bd62/server/core/src/main/java/dev/monaka/tracking/TrackerBodyAssignments.kt)
  and [runner](https://github.com/MonakaVR/MonakaVR/blob/17c4c9bb712b425bd72911837c7df010dee5bd62/server/core/src/main/java/dev/monaka/tracking/ObservationBackendRunner.kt).
- [HTC API header](https://github.com/ViveRoboticsSoftware/vive-ultimate-tracker-sdk/blob/8685112780734c2513c66d53b9eb104db47afcf9/include/ultimate_tracker.h)
  and [IPC reference](https://github.com/ViveRoboticsSoftware/vive-ultimate-tracker-sdk/blob/8685112780734c2513c66d53b9eb104db47afcf9/tracker_ipc_client_ref.py).
- [RE ACK namespace annotations](https://github.com/shinyquagsire23/vive_ultimate_tracker_re/blob/11ae3cbfd9a331694c72890df7dced77248b4ba6/tracker_toybox/enums_horusd_ack.py).
- [Microsoft pipe security](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights)
  and [CreateNamedPipe](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createnamedpipea).
- [Pinned identity research report](https://github.com/orzkwsk/ViveUltimateTrackerBridge/blob/d78dab2041266cd4cad462bf4784abc3131b556c/docs/hil/altra-stable-identity-offline-research.md)
  and [historical HIL results](https://github.com/orzkwsk/ViveUltimateTrackerBridge/blob/d78dab2041266cd4cad462bf4784abc3131b556c/docs/hil/altra-stable-identity-hil-results.md).

## Search coverage and limitations

The scoped CODE audit searched current and selected historical `src`, `include`, `tools`
for named pipe/AF_UNIX/command-queue patterns, then inspected the located DebugConsole
queue and diagnostic-service diff. The primary runtime has no production local control
API in those paths. The debug queue provides an owner-thread pattern but lacks production
bounds/authentication/handle lifecycle. Registry/pairing/console are real primary modules;
there is no need to import an old reverse-engineering backend.

GitHub repository discovery queried `user:orzkwsk Vive` (primary repository found),
`user:orzkwsk Altra` (no results), `org:MonakaVR tracker` (MonakaVR found) and
`org:MonakaVR HIL` (no results). These are scoped search results, not proof that no other
private/renamed experimental repository exists. Historical README also names pyvut forks;
they were not independently audited here and are not used to justify any architecture
or command semantics. Public SDK/RE refs are deliberately pinned historical references,
not claims about their current default-branch HEADs.

Local Downloads inventory identified MonakaVR-HIL/Observe directories, but sandbox Git
access to the sibling MonakaVR-HIL checkout failed; no files or refs in sibling checkouts
were modified and no local uncommitted sibling state is evidence here. Fixed GitHub
MonakaVR/Bridge source and this repository's sanitized historical evidence supply the
cross-repository scope. Large vendor archives, Hub logs/cache and private raw HIL were
not opened for this task; prior correlations remain report-level historical evidence.

Web fetch of pinned public GitHub pages returned cache misses; authenticated read-only
GitHub file tools retrieved the exact blobs successfully. Branch URL with encoded slash
was rejected, so branch identity was obtained through the read-only branch collection.
No failed fetch is counted as an inspected source. All conclusions are bounded by the
files/commits listed above; no claim of an exhaustive global code search is made.

## Reuse summary

- **Reusable:** owned HID/decoder/backend event model; adapter/publisher; session/revision
  coordinator; existing fake transport and software test fixtures.
- **Partially reusable:** PairingController observe/tick/cleanup; Devices filtering and
  motion evidence; SetupConsole parsing/presentation; historical queue concept; exact-byte
  research parser/validation policies after qualification and narrow review.
- **Historical only:** interrupted identity HIL/correlations, old experimental build/PCAP
  provenance, SDK/RE command vocabulary, prior role/store UX.
- **Unsafe / obsolete for production adoption:** raw feature/SLAM/reset/reboot IPC exposure,
  detached unbounded debug queue, startup PairDevice/RestartRf replay, slot/RF-as-stable-ID,
  RF-based body assignment and unverified NA acquisition.

New components required are ServiceControlModel, immutable snapshot/operation records,
runtime-handle registry, bounded authenticated queue, reconnect/control coordinator,
crash-cleanup policy/journal, local pipe adapter and service client. They are proposals,
not implementations delivered by this documentation checkpoint.
