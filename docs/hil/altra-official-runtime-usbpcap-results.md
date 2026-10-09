# Official B controlled Pair new / normal reconnect: Connected-equivalent PASS

2026-10-10 JST. **Official Pair: PASS. Official reconnect: PASS** under the instruction's
Connected-equivalent gate: official UI 1/1 plus fresh association/device-info/data stream.
**Map Ready and valid optical tracking: NOT CONFIRMED.** UI requires setup; all decoded
pose statuses are Lost 0x03/0x13. Numeric pose updates are not a valid tracked pose claim.

Source of Truth: `MonakaVR_Altra_Official_B_Pairing_Baseline_Reconnect_HIL_Codex指示書.md`,
SHA256 `7c692c20fae9d8a8c0199d943ca520743a73c22461e2f8213286e8b30bbd313b`.
Research branch `research/altra-stable-identity-hil`, starting checkpoint
`0dd677bc0af2fca0936cc1321fe0926433dad4cd`; prepared-lineage ancestry verified.
Existing other checkout and historical evidence preserved. Production code unchanged.

## Acquisition and physical binding

Human confirmed A/B OFF, both charging USB removed, dongle present before capture.
Current PnP target/parent exactly matched private inventory; current extcap interface,
config, unique selected device and TShark preference keys were checked, without guessing.
Tools retained installed versions/hashes. VIVE Hub, ViveHubServer, ViveTrackerServer and
Virtual Desktop were running; Streaming Service was Stopped, SteamVR/ViveAgentService
absent in inspected inventory. No service/runtime restart was performed. No direct-HID
actor or concurrent capture was present; actual HID handle ownership was not proven.

Each new capture's valid header, 0bb4:0350 descriptor and same-target live traffic were
verified before the physical operation. TShark used installed USBPcap extcap and normal
duration autostop (Pair 360 s; reconnect 300 s), exit 0. No legacy launcher/q injection.
A shared-read snapshot was needed because a default file read lacked writer sharing;
this was an inspection restriction, not capture corruption or a second hardware capture.

Automated Hub input encountered parent/child window geometry and detected-user-input
errors. Human performed one official Pair new. Initial gesture confirmation was followed
by blue then breathing blue; later the human confirmed the effective blue-state 2-second
hold changed B to green then breathing green, with A still OFF. New DevicePaired and
N-burst lie inside the B-only gesture request-to-effective-confirmation interval.
Do not bind B from historical RF/slot/cache. Fresh pair RF and identity-envelope RF match
within each capture. Both identities exactly match immutable HIL-05 B raw references,
independently verified against the original raw envelope and saved candidate bytes.

UI became 1/1 / setup required. The Connected-equivalent observation receipt was saved
before capture end; capture continued about 18 seconds afterward. Human then completed
the same pairing dialog using Done, kept B ON, and reconnection capture started with UI
1/1 and pairing dialog closed. Done/disable is in the uncaptured inter-capture interval.

In the second capture, human confirmed B OFF / A OFF; a monotonic wait exceeded 10 s
before requesting normal B ON, with no pairing gesture or Pair new. Human confirmed
normal ON, no Pair operation and A OFF. Fresh disconnect -> association -> burst ->
pose stream establishes recovery; UI alone is insufficient because its 1/1 value was
still present at the OFF observation. No Unpair, Remove, reset, map setup or DB edit.
Bridge/direct-HID processes were never launched and no manual vendor command was sent.
Capture ended normally with B kept ON and final official UI still 1/1 / setup required.

## Observed sequence and four-way matrix

All times below are relative to the first packet of the corresponding capture.
Counts refer to classified USB records; submission/completion are not independent commands.
SetFeature data is exposed by this TShark as `usb.data_fragment`. The private extractor
passes those exact bytes into the existing sanitizer's payload input when other fields
are empty. It never concatenates fields. The official 0x18 TX uses payload-kind 0x03,
where current Backend uses 0x04. Strict length-consistent allowlisted ASCII is reported
as a supplemental observation; sanitizer command remains unknown and destination
semantics are unverified. Unknown host payloads remain unknown.

| Phase | Official Pair | Official reconnect | Current Backend (static) | Passive guard (static) | Confidence |
|---|---|---|---|---|---|
| PairDevice | on at 133.665 s, 3 selected receiver positions; off not captured | on/off not observed in this capture | explicit enable/disable, bounded cleanup | all feature exchanges suppressed | High bytes; disable lifecycle incomplete |
| OFF host action | N/A | behavior 2, flag 1, one selected position at 46.386 and 46.393 s; surrounds disconnect 46.389 s | DeviceUnpaired -> slot-local behavior 1 / PWS-off | suppressed | High raw ordering; RestartRf label is historical CODE candidate |
| PWS | behavior 1 not observed | behavior 1 / PWS-off not observed | observed-disconnect PWS-off | suppressed | Capture-local absence; no necessity/safety inference |
| RF/status | 36 RF status records; host 0x1e query variant before Pair enable | 30 RF records; host 0x1e query variant after OFF | status does not trigger configureOnPair | receive only; queries suppressed | RF words/slot/RF private; no bit/state inference |
| DevicePaired | 296.045 s | 115.552 s after OFF/ON | triggers configureOnPair before callback | receive event only | High; event name is not registration creation proof |
| NA | exact NA not observed | exact NA not observed | no NA request path | suppressed | No NA causal/refresh/safety claim |
| N-burst | NANA 296.525 -> NADS 296.557 -> NASS 296.589 -> NASI -> NAPI -> NAV1 -> NARI -> NAGN -> NAZZ 296.813 s | NANA 115.888 -> NADS 115.920 -> NASS 115.952 -> NASI -> NAPI -> NAV1 -> NARI -> NAGN -> NAZZ 116.184 s | incoming info accepted | receive only | High; fresh B values and RF correlation |
| Slot/post-pair init | after NAZZ: ANI<slot> 296.821; unknown; Wc<country>; unknown behavior 6 twice | after NAZZ: ANI<slot> 116.185; unknown; Wc<country>; further unknown | DevicePaired -> ARI1/ATM-1; host ATH1/AWH1/ANI0/ATM20, client ATH0/AWH0/ANI(slot)/ATM11 | configure flag and central guard suppress sends | High literal/order; unknown semantics retained |
| FW1/FW2/mode | FW2 296.846 -> ATM21 296.851; FW1 not observed | FW2 116.199 -> ATM21 116.201; FW1 not observed | NAZZ -> FW1; host ATM20/client ATM11 | suppressed | High exact ASCII; applicability/safety unisolated |
| First pose after fresh pair | 298.741 s, pair +2.696 s; burst complete then host init then pose | 118.536 s, pair +2.984 s; pre-OFF pose excluded | existing decoder; successful connection not a PairingController result condition | receive only | High data arrival; Lost status, map Ready unconfirmed |

ARI1/ATM-1/ATH1/ATH0/AWH1/AWH0/ATM20/ATM11 were not exact observed host labels here.
ANI is observed as the fresh B slot alias, not a persistent identity. Behavior 6 remains
unknown: it is not labeled Unpair/delete/reset and is not copied into Backend policy.
The 0x1e host query variant is outside the existing classifier's framing support
(`malformed-vendor` label); this does not mean a corrupt pcap or invalid official command.

## RF diagnostic transitions

These aliases compare whole opaque RF status vectors; they do not map bits to SDK state.

| Capture | Relative transitions |
|---|---|
| pairing | RF-STATE-1 at 4.144 s -> RF-STATE-2 at 134.152 s -> RF-STATE-3 at 304.159 s |
| reconnect | RF-STATE-4 at 3.857 s -> RF-STATE-5 at 53.858 s -> RF-STATE-4 at 123.864 s |

Fresh Pair/reconnect RF diagnostic bytes are equal to each other and HIL-05 B.
This equality is not the basis for physical B binding; the B-only operation window is.

## Identity and pose evidence

| Check | Pair | Normal reconnect |
|---|---|---|
| NADS-B vs HIL-05 B | byte-exact equal | byte-exact equal |
| NASS-B vs HIL-05 B | byte-exact equal | byte-exact equal |
| Pose records after fresh DevicePaired | 7671 | 22699 |
| Finite position/quaternion samples | all 7671 | all 22699 |
| Distinct numeric samples | 7629 | 16273 |
| Status | 0x03 / 0x13 (Lost) only | 0x03 / 0x13 (Lost) only |

Pair vs reconnect NADS and NASS also individually byte-exact equal. Acquisition is now
demonstrated through this official B Pair/reconnect, not through passive/direct-HID.
Both candidates remain Class B overall: other unresolved stability/acquisition scenarios,
explicit unpair and broader rediscovery gates are not completed by this single B trial.

## Timing and validation limits

Private sidecar contains immediate request/confirmation receipts, UTC and monotonic
ticks, launch/exit timestamps. Physical click/gesture/switch times themselves were
not directly measured. Pair enable appears before the later human click acknowledgement
and handoff request receipt, so that receipt must not bound the actual click. Effective
green gesture confirmation arrived after capture ended; the fresh pair is nevertheless
inside the initial B-only request-to-effective-confirmation interval. Public JSON keeps
relative receipts and these limitations, without absolute timestamps. OFF/ON are bounded
by human request/reply intervals, plus the measured >10 s OFF-confirmation wait.

| Evidence | Size / packets | SHA256 |
|---|---|---|
| PAIR-B | 1438800 bytes / 15636 | `47b7495f93b897a1ed1d51ac147d52ac8ce49516e51e740081f1c5f92db55067` |
| RECONNECT-B | 5265616 bytes / 57232 | `157d896f08b9a0471419431e34a5a1d74451016c8d251bb6fa589b191677d69d` |

Both pcapng files parse offline with exit 0. Sanitizer unit tests 8/8 PASS; existing
fake-transport/offline protocol/backend_pairing/setup_pairing/identity_capture/
identity_compare/identity_offline CTest 6/6 PASS. These fake tests do not open hardware.
Production build NOT RUN; production source unchanged. Leakage validation is recorded
in the public sequence metadata after scanning actual private candidates/RF/topology,
raw payload representations and absolute timestamps. Raw pcapng/CSV, inventory,
identity bytes, topology, operator receipts and exact commands stay gitignored/private.

## Final classification

```text
Official Pair: PASS (Connected-equivalent, setup required)
PairDevice: on observed; disable not captured
PWS: not observed
Post-pair init: after N-burst; ANI<slot>, Wc<country>, unknowns, FW2, ATM21
NA: not observed; DO NOT SEND from Bridge
Device-info burst: full nine-token burst observed
NADS: fresh NADS-B; HIL-05 byte-exact equal
NASS: fresh NASS-B; HIL-05 byte-exact equal
FW1: not observed
FW2: observed
First pose: after N-burst and FW2/ATM21; Lost status only
B stable identity comparison: NADS equal; NASS equal
Official reconnect: PASS (normal OFF/ON, no Pair operation)
Reconnect PairDevice: not observed
Reconnect PWS: not observed
Reconnect slot init: ANI<slot> and further init after NAZZ
Reconnect NA: not observed
Reconnect N-burst: full fresh burst; NADS/NASS both equal to HIL-05 and Pair
Reconnect pose: restored changing finite data; Lost status only
Most likely acquisition blocker: B / host-recovery candidate;
  official behavior 2 and init differ from current/passive paths.
  Causal discrimination remains F; no sole-effect experiment.
NADS stability: Class B; this B Pair/reconnect equality added
NADS acquisition: official B acquisition achieved; passive refresh unresolved
NASS stability: Class B; this B Pair/reconnect equality added
NASS acquisition: official B acquisition achieved; passive refresh unresolved
Current PairingController correctness: Unknown end-to-end;
  bounded software lifecycle acceptable in fake tests
Current reconnect path correctness: Unknown end-to-end;
  current PWS differs from official behavior 2; no production fix inferred
Next action: review official behavior 2 / 0x18 kind 3 / FW2-ATM21 differences
  and unknown commands offline; require separate gates before active Bridge HIL.
```

No integration merge or production cherry-pick. No additional hardware trial follows
this acquisition. Prior STOP evaluations below are historical and remain preserved.

---

# Altra official extcap Health Gate PASS; official B baseline STOP

## Current run, 2026-10-09 to 2026-10-10 JST

**Health Gate: two consecutive PASS. Official B baseline: Connected/Ready not confirmed — STOP.**
Power-cycle reconnect and the successful official baseline/reconnect 3-way comparison are **NOT RUN**.

Source of Truth: `MonakaVR_Altra_Extcap_Health_and_Official_Runtime_HIL_Codex指示書.md`,
SHA256 `e33df2ff8421f776eb5613bfe36d112286e9ec22e2bd8cfd234fa2cd25bd5833`.
Research checkpoint `4209b2500b4d6b6bf3167627ce7a392994bd4225`, branch
`research/altra-stable-identity-hil`; prepared-lineage ancestry verified.

Legacy launcher path: **NOT USED**. No console `q` injection, custom FIFO,
USBPcapCMD direct capture control, Bridge/direct-HID launch or manual vendor transmission.
TShark managed its installed extcap child and capture lifecycle. USBPcapCMD native exit code
was not a success requirement. GUI fallback was not needed.

### Discovered inventory and capture method

Installed Wireshark/TShark/dumpcap **4.6.6**, USBPcapCMD/extcap **1.5.4.0**.
TShark `-D` and installed USBPcapCMD `--extcap-interfaces` both enumerated five USBPcap
interfaces. `--extcap-config` was queried for every returned interface, yielding:
`--snaplen`, `--bufferlen`, `--capture-from-all-devices`, `--capture-from-new-devices`,
`--inject-descriptors`, and `--devices`. Actual interface names, device values, config,
paths and tool hashes are private. Public target alias: **DONGLE-BUS / DONGLE**.

Current `0bb4:0350` PnP parent matched exactly one current USBPcap root via zero-access,
read-only hub-symlink metadata query. Its current extcap config contained one direct USB
input device. No dongle HID open was used to establish this selection.

`-i <actual interface> -G currentprefs` exposed the actual TShark preference keys.
Those discovered keys were supplied with `-o` to select that device, disable all-device
and new-device capture, and enable descriptor injection. No preference or driver setting
was persisted. Each health capture used `-a duration:5 -w <new private pcapng>`.
Exact command/exit/stdout/stderr receipts are private; topology-bearing keys are not published.

Human confirmed A/B OFF, both charging USB disconnected, official dongle connected.
VIVE Hub, ViveHubServer and ViveTrackerServer were running; prohibited direct-HID/capture
actors were absent in the preflight inventory. This does not prove exclusive handle ownership.
SteamVR/Virtual Desktop were not restarted. No install, driver/filter/registry change or reboot.

### Fresh Health Gate receipts

| Gate | Health Run 1 | Health Run 2 |
|---|---|---|
| Method / TShark lifecycle | official extcap / duration 5 s | official extcap / duration 5 s |
| Fresh capture | HEALTH-1, 912 bytes | HEALTH-2, 892 bytes |
| Header | valid pcapng, `0a0d0d0a` | valid pcapng, `0a0d0d0a` |
| TShark capture / offline parse exit | 0 / 0 | 0 / 0 |
| Packets | 8 | 8 |
| Target correlation | `0bb4:0350` descriptor + same-tuple live interrupt | `0bb4:0350` descriptor + same-tuple live control |
| Non-injected traffic | 64-byte interrupt report | GET_DESCRIPTOR setup/completion after initial injected records |
| Result | **PASS** | **PASS** |

Run 2 did not contain a nonempty interrupt report; live target control traffic satisfies
this task's capture-health requirements. It is not RF/pose or connection evidence.
The 5 seconds is the TShark autostop condition, not the first-to-last packet span.
Historical captures were not reused as health proof.

### Official runtime baseline attempt

After both PASS results, a new TShark official-extcap capture used `-a duration:180`.
File creation, the target descriptor and incoming packets were checked before asking for B ON.
Official UI showed dongle connection mode and one existing disconnected tracker row.
The human was asked to keep A OFF, power B ON normally, and avoid pairing.
The human reported **B ON, blue/blue flashing**. Subsequent official UI checks still showed
**not connected**, **tracking 0 / 1**, and **no tracker connected**. Connected/Ready and
tracking/pose restoration were not confirmed. No Pair new/setup/identify/turn-off control was used.

TShark ended normally by duration: fresh valid pcapng **4,368 bytes / 46 packets**,
offline parse exit **0**, target descriptor and same-tuple traffic verified.
Existing sanitizer classified **18 RF status** records and **28 not classified** records
(USB control and empty interrupt records). No device-info burst, DevicePaired event, pose,
PairDevice, PWS, RestartRf, NA, FW1/FW2 or classified host vendor sequence appeared in this capture.
These absences concern this unsuccessful attempt only, not a successful official reconnect.

Timing limitation: the physical ON timestamp was not measured. The operator receipt was
written after capture ended, so B ON overlap with the capture is not precisely time-bound.
Do not describe the whole 180-second capture as a B-ON observation or a reconnect deadline.
No RF/slot was bound to physical B. UI/LED does not prove registration loss.

**STOP per the current instruction's baseline gate.** Reconnect was not attempted and no
Pair/Unpair/manual command was added. The capture infrastructure is established; the
remaining official connection/identity acquisition issue is unresolved.

### Comparison boundary

A successful 3-way diff is **NOT RUN**, because the baseline connection gate failed.
The table records failed-attempt observations separately from static facts at this checkpoint.
Backend and passive guard were not executed against the dongle.

| Stage | Official baseline attempt | Official reconnect | Current Backend (static) | Passive guard (static) | Interpretation |
|---|---|---|---|---|---|
| PairDevice | not observed in capture | NOT RUN | explicit pairing/cleanup; no ordinary startup enable | feature exchanges suppressed | pair-free successful reconnect unverified |
| PWS-off | not observed in capture | NOT RUN | slot-local DeviceUnpaired recovery | suppressed | no causal recovery comparison |
| RF/status | 18 RF status reports; B binding absent | NOT RUN | RF status does not trigger configureOnPair | receive only; queries suppressed | RF reports do not establish B Connected |
| Slot initialization | no classified host init observed | NOT RUN | DevicePaired -> ARI1/ATM-1; host ATH1/AWH1/ANI0/ATM20, client ATH0/AWH0/ANI(slot)/ATM11 | configure flag and central guard suppress writes | necessity remains unknown |
| NA / device info | NA and N-burst not observed | NOT RUN | no NA request path; incoming info accepted | receive only | refresh safety remains unknown |
| FW1/FW2/RestartRf | not observed | NOT RUN | NAZZ -> FW1; no safe new recovery inferred | feature exchanges suppressed | no official successful sequence available |
| First pose | not observed | NOT RUN | existing decoder preserved | receive only | tracking not confirmed |

NA context / preceding event / response / N-burst / state: **N/A, NA not observed in this capture**.
DO NOT SEND remains in force. PWS/PairDevice/FW1/FW2 observed: **not observed in this capture**.
Acquisition blocker: **F / distinguish不能**. Safe reconnect and device-info refresh: **Unknown**.
NADS and NASS: **Class B / acquisition unresolved**, unchanged.

### Evidence, validation and publication

Raw pcapng, raw CSV, serial/RF/topology, exact commands/config, process/PnP inventory and
operator receipts remain private under gitignored `hil-private/official-runtime-usbpcap/`.
Public sequence contains sanitized events with aliases and relative times only. Existing
historical sequence/events and reports are preserved below and in the JSON's historical sections.

| Alias | Capture SHA256 |
|---|---|
| HEALTH-1 | `809865cedff408f57268509054aaa4ed0b195744387e373ddbae4b30cecca319` |
| HEALTH-2 | `a9b672ac1e4d3e8e55baa116cb9157e433332c45250937cad0beb206f31d9282` |
| BASELINE-ATTEMPT | `8f6c974d5bd0855e4748eb24d3e2a7e3dff3cf0488c08d343189a57fbe494f72` |

Validation: existing offline USB sanitizer **8/8 PASS**; existing fake-transport/offline
CTest protocol/backend_pairing/setup_pairing/identity_capture/identity_compare/identity_offline
**6/6 PASS**. Production build **NOT RUN**, production code unchanged. Real official
connection/reconnect hardware success **NOT CONFIRMED / NOT RUN**.

Privacy validation **PASS** for the new two-file content; raw captures/CSV are gitignored,
and historical JSON/report content is preserved.

Only this sanitized report and sequence are published to the research branch.
No integration merge or production cherry-pick. Next action is a separately scoped investigation
of official B normal connection and precise physical-operation timing; this run is stopped.
The legacy launcher's historical next-action text below is superseded for this task.

---

# Historical launcher and capture attempts

# Altra USBPcap launcher contract: Run 0 STOP

## Launcher contract retry, 2026-10-09 JST

**Run 0 native GUI console test FAIL; Health Run 1/2 NOT RUN.**
USBPcapCMD captureを起動していない。console `q`正常停止契約は未成立。
指示書§6/§25に従い、hardware capture、B ON、baseline、reconnectへ進まず停止した。
USBPcap/driver障害との判定はしない。

Source of Truth: `MonakaVR_Altra_USBPcap_Launcher_Contract_Health_Retry_Codex指示書.md`。
SHA256 `957acbfdba9001bbfa224d0584a027afebec9b8906f31777349915a21e4f6c39`。
Research branch `research/altra-stable-identity-hil`、checkpoint
`3044796f86465beeb966fb2645a0432878d04d25`、prepared lineage ancestry確認済み。
他checkoutのfeature branch / 未追跡attachmentを保持した。

### Implementation and upstream basis

Research-only `scripts/run_usbpcap_health_capture.ps1` と
`scripts/usbpcap_health_contract.psm1` を追加。production Backend変更なし。

Launcherは管理者権限、専用console input、単一control/address、bounded duration、
新規private output、同時capture/Bridge actor不在を検査する。
`Start-Process -NoNewWindow -PassThru` のprocess object/handle/PIDを保持し、
fileを5秒以内に待つ。captureは5–10秒、console KEY_EVENT key-down/up `q` を投入し、
exitを10秒以内に待つ。native exit codeとfile flush後の検証を要求する。
期限超過はFAILにし、保持したprocess objectだけをcleanupする。
通常stdin pipe/fileへの `q` redirectは使用しない。

各stateをprivate JSONへ更新保存する:
`Preflight -> Starting -> ProcessCreated -> WaitingForFile -> Capturing -> StopRequested -> Exited -> Validating -> Succeeded/Failed`。
timeout時は`CleanupRequired`も記録。private outputの既存fileは拒否し、
USBPcap側のCREATE_NEWに任せる。public receiptはfield/failure-code allowlistでredactする。

検証はknown pcap/pcapng magic、size、SHA256、bounded offline tshark exit 0、packet > 0、
指定tupleの `0bb4:0350` descriptorと同tupleの非empty interrupt trafficを要求する。
descriptor injectionだけではPASSにしない。
このfile validationの実captureでの動作は**NOT RUN**。

安全guard: **同じmodule SHA256のnative GUI Run 0でelevation・console q・native exit 23を
実測PASSしたprivate receiptがなければ、launcherはUSBPcapCMDを起動しない。**
今回そのreceiptは存在しない。このlauncherを運用可能・contract成立済みとは報告しない。

USBPcap upstreamをread-only確認し、commit
`477b6edcbd7e99a47f77afc0c4168a9ebee603bb` をpinした。
[cmd.c](https://github.com/desowin/usbpcap/blob/477b6edcbd7e99a47f77afc0c4168a9ebee603bb/USBPcapCMD/cmd.c)
のGUI entry point、parent console attachと元のstdin復元、elevation分岐/runas worker、
worker wait、CREATE_NEW、ReadConsoleInput key-down `q`停止を設計根拠とした。
source audit hash/receiptはprivate。第三者sourceをpublic実装へコピーしていない。

### Run 0 observations

HumanがA/B OFF、両charging USB disconnected、official dongle connectedを再確認。
launcher PowerShell全体を通常UACで昇格した。input GetConsoleModeは成功。
hardwareを開かない自作GUI childでconsole attach/input/exitを検証した。
managed GUIとnative GUI、process API/handle継承の候補をdry検証したが、
q正常停止PASSは得られなかった。候補APIの証跡はprivateに保持した。

最終native GUI fixtureは既存MSVC 14.51でbuild。既存USBPcap binaryと同一buildではない。
final `Start-Process -NoNewWindow -PassThru` testでprocess object/handle保持、
initial HasExited=false、終了後native exit **31**を取得した。
fixture内のAttachConsoleは成功したが、元のstdinはNULLで、復元後GetConsoleMode=false。
fixtureは入力不可として終了したため、qは送っていない。
launcherのconsole membership検査時にはchild退出が進み、`DRY_ATTACH_FAILED`として停止。
最終runのforced cleanupはなし。

**exit 31は自作fixtureの入力拒否コードであり、USBPcapCMDのexit codeではない。**
今回USBPcapCMDをcaptureで起動していないため、installed USBPcapCMDのstdin状態や
worker/driver不具合へこのfixture結果を一般化しない。前回のno-file/exit未取得も維持する。

### Required outcome matrix

| Item | Result |
|---|---|
| Launcher elevation | PASS: elevated PowerShell、console input確認 |
| Process tracking / native exit | PASS for dry child only; USBPcapCMD NOT RUN |
| Normal console q stop | NOT ESTABLISHED: dry child input invalid; q not sent |
| Graceful exit timeout | implemented 10 seconds; mock timeout test PASS; real USBPcapCMD NOT RUN |
| New output / CREATE_NEW handling | pre-existing output rejection tests PASS; real capture NOT RUN |
| Run 0 | **FAIL — STOP**, native GUI input contract unresolved |
| Health Run 1: tracking/file/q/exit/header/tshark/packets/target | **all NOT RUN** |
| Health Run 2: tracking/file/q/exit/header/tshark/packets/target | **all NOT RUN** |
| Capture SHA256 / packet count / target confirmed | N/A / unknown / not observed |
| Extcap pipe | NOT RUN |
| Official B baseline attempted / result | false / NOT RUN |
| B power-cycle reconnect attempted / result | false / NOT RUN |
| Current Backend vs passive guard vs official trace | new measured comparison unavailable; historical static matrix retained |
| Altra conclusions changed | No |
| NADS / NASS | Class B / acquisition unresolved (both retained) |
| Altra acquisition blocker | F / distinguish不能 (retained) |

SteamVR/Virtual Desktop/official runtimeを終了・再起動していない。
終了時VIVE official componentsとVirtual Desktopはinventory上存在。
Bridge/direct-HID/capture actorsはinventory上不在。実際のHID handle ownershipは未実証。
Pair/Unpair/vendor command、driver/install/registry変更、reboot、新規software導入なし。
新規raw pcapなし。dry launcher/native child receipts、process inventory、source auditを
gitignored private evidenceとして保持し、raw PID/path/serial/RF/topologyは公開しない。

### Validation and next action

- Launcher offline tests: **71 assertions PASS**; mocked child lifecycle/timeout/receipt,
  actual OS child exit 23/timeout、elevation/inputs/output/console contract rejection、magic、
  target-unconfirmed fail、private field/error redaction。mock PASSはhardware証明ではない。
- Native GUI fixture build: **PASS** (existing MSVC; no installation)。real console contract test: **FAIL**。
- Existing offline USB sanitizer: **8/8 PASS**。
- Production C++ backend build/HID hardware validation: **NOT RUN** (production source unchanged)。

Public成果はresearch launcher/tests、このsanitized reportとsequence metadataのみ。
research branchへ通常commit/pushし、integrationへmerge/cherry-pickしない。

Next action: 別のlauncher研究で、installed USBPcapCMDを変更せず、same-console stdinと
ReadConsoleInput `q`停止を安全に実測できる方法を解決する。
Run 0 PASS -> 新規Health Run 1 PASS -> 新規Health Run 2 PASSの後だけB baseline/reconnectへ進む。
今回の停止後に実captureやTracker操作は追加しない。

---

# Historical: Altra official runtime USBPcap capture transport stop

## Historical: direct-to-file Health Gate, 2026-10-09 JST

**STOP: Direct capture 1 FAIL (output file not created).**
Direct capture 2 / official B baseline / OFF→ON reconnect: **NOT RUN**。
B ONを依頼していない。新しいAltra wire sequence・protocol結論はない。

Source of Truth: ユーザー指定
`MonakaVR_Altra_USBPcap_Health_Gate_and_HIL_Resume_Codex指示書.md`。
SHA256 `1e74acdde1cdb720d17b9de77841fc368d1aae35c006b6117a1a41e68e019e7a`。
指定checkpoint `1aa753ffc132223cec1a2507efdaf75645a0750f` の既存
`research/altra-stable-identity-hil` worktreeで実施。prepared lineage ancestry確認済み。
主checkoutのfeature branchと未追跡attachmentは保持した。

### Read-only preflight

人間が A OFF / B OFF / charging USB disconnected / official dongle connected を確認。
USBPcap driver RUNNING、USBPcapCMD/USBPcap extcap 1.5.4.0、Wireshark/tshark/dumpcap
4.6.6を確認。既存extcap entriesはUSBPcapCMDとetwdump、USBPcap root entriesは5。
extcap interface/configの**列挙だけ**を使用し、pipe captureは行っていない。

USBPcapCMDは `C:/Program Files/USBPcap/USBPcapCMD.exe`。
SHA256 `8b13173e9453fda9914d707c732880de44081e20e1113b3a2c827abd7a28b15d`。
tshark/dumpcap/Wiresharkは既存 `C:/Program Files/Wireshark/` 配下。
各version/hash、driver/service、process、PnP/topology inventoryをprivate保存した。
実行callerはnon-admin。新規install、driver/filter/registry変更、rebootなし。

Capture直前・失敗後にViveHubServer / ViveTrackerServerを確認。
VHConsoleはinventory途中に一度観測し、capture直前・失敗後には不在。
VIVE Streaming Service STOPPED、ViveAgentService process/serviceは確認した一覧に不在。
SteamVR vrserver/vrmonitorは不在、Virtual Desktop Streamer/serviceはRUNNING。
SteamVR / VD / official runtimeの終了・再起動は行っていない。
Bridge/direct-HID/pyvut/debug consoleと同時capture processは確認した一覧に不在。
禁止executableを起動していない。official runtimeを唯一のHID ownerとして扱い、
exclusive-open / handle contention testは行っていない。実際のhandle ownershipは未実証。

VID/PID `0bb4:0350` のpresent-only PnP parentと、既存USBPcap capture-controlの
read-only hub-symlink queryを正規化後に完全一致で照合し、target rootを一意に確定。
そのrootの唯一のdirect USB input deviceからcapture addressを選択した。
USBPcap controlにはzero-access open + hub-symlink queryだけを行い、dongle HIDをopenしていない。
生root/address/interface/PnP identifiersはprivate。pcapでのdescriptor一致は未確認。

初回の無引数USBPcap enumerationは自身のconsoleを作るためredirected stdinを読まず、
capture未選択のまま待機した。その自分のprocessだけを終了して、非対話のextcap inventoryへ移った。
このenumerationはcapture試行でもvendor command送信でもない。

### Direct capture 1 receipt

`USBPcapCMD -d <verified-control> --devices <selected-address> --inject-descriptors
-o <private-new-file.pcap>` を実行。出力はstdout/pipeでなくprivate filenameを指定した。

| Gate item | Result |
|---|---|
| File created / size > header | FAIL: destination file absent; size N/A |
| Valid pcap/pcapng header / first 32 bytes | NOT AVAILABLE: file absent |
| tshark parse | NOT RUN: file absent |
| USB packet count > 0 | NOT CONFIRMED; count unknown, zeroとして記録しない |
| Target `0bb4:0350` traffic | NOT CONFIRMED; PnP inventoryだけではcapture PASSにならない |
| Overall | **FAIL — STOP** |
| Direct capture 2 | **NOT RUN — direct capture 1 failed** |
| Extcap pipe | **NOT RUN — previous pipe path not retested** |
| Chosen production research capture path | **NONE — Health Gate not established** |

PowerShellのGUI executable呼び出しはworker完了を待たずに戻った。
`$LASTEXITCODE`は空で、USBPcap native exit codeは取得できていない。
launch shell exit 0をUSBPcap capture成功として扱わない。
直後のread-only確認でUSBPcap worker、consent process、同時capture process、output fileは不在。
成功した5–15秒captureは成立していない。
stdoutにはshellの空のnative-exit表示だけがあり、USBPcap worker stdout/stderrは取得できていない。

分類: **DIRECT CAPTURE LAUNCH / LIFETIME FAILURE (underlying worker cause undetermined)**。
launcherがworkerを待たなかった事実と、worker/driver側の原因未確定を分離する。
valid header不成立を新しい `magic=0x00000000` の観測と呼ばない。
driver故障、再install/reboot必須、extcap/pipe原因確定とも判断しない。
指示書§13/§27に従い、追加captureやlauncher再試行を行わず停止した。

### HIL / interpretation retained

| Item | Result |
|---|---|
| Official baseline: B ON attempted / Connected / tracking | NOT RUN / NOT TESTED / NOT OBSERVED |
| Baseline N-burst / host sequence | Unknown / unavailable |
| Power-cycle reconnect: attempted / Pair used / reconnected | NOT RUN / NONE / NOT TESTED |
| Reconnect N-burst / host sequence | Unknown / unavailable |
| NA / PWS / PairDevice / FW1 observed from official traffic | Unknown: no valid official trace |
| Bridge manual vendor command | NONE |
| Pair new / Unpair / Remove tracker | NOT RUN |
| Most likely Altra acquisition blocker | F — distinguish不能を維持 |
| NADS stability / acquisition | previous Class B / HIL_REQUIREDを維持; acquisition unresolved |
| NASS stability / acquisition | previous Class B / HIL_REQUIREDを維持; acquisition unresolved |

baseline / reconnect / Current Backend / passive guard差分は、下の歴史的matrixの
未観測列を維持する。file未生成からcommand absenceやB接続失敗を推論しない。
古いpreparation 28 packetsを今回のHealth Gate証明に再利用しない。

### Validation / private evidence / next action

raw evidenceはgitignored `hil-private/official-runtime-usbpcap/health/` の
inventory、operator receipt、failure receiptのみ。今回pcap/CSVは生成されていない。
Capture SHA256: N/A (no file)。topology/serial/RF/descriptor serialは公開しない。

既存Windows Release test targets build PASS。実機を使わないfake-transport/offline CTest
`protocol|backend_pairing|setup_pairing|identity_capture|identity_compare|identity_offline`
**6/6 PASS**。既存offline USB sanitizer unit tests **8/8 PASS**。sanitizer/test source変更なし。
これはUSBPcap/HIL成功を意味しない。対象2 public filesのprivate-value leakage scan **PASS** (45 identifiers / 515 representations; 保存raw payload 329件を含む)。
production Backend / shared protocol変更なし。成果はsanitized report / sequence metadataのみ。
research branchへcommit/pushし、integrationへmergeしない。

Next action: このrunのcapture/HILは終了。次の別taskで、既存USBPcapをprivate fileへ
起動するlauncherのprocess lifetime・明示wait・停止方法・native exit receiptを先に確定し、
A/B OFFのHealth Gateを新規runとして実施する。現在のFAIL後に追加操作を行わない。

## Historical: previous extcap run at checkpoint 1aa753f

以下は前回runの記録。古いcaptureや当時のprocedureを今回の成功・実行権限として扱わない。

2026-10-09 JST。Research base `1fbe8f8293ada00221a01afe9d558835c7331ac0`。
**STOP: baseline capture transport failure。正常B connect/reconnect wire sequenceは未取得。**

## Scope / Source of Truth

ユーザー指定の `MonakaVR_Altra_Official_Runtime_USBPcap_Reconnect_HIL_Codex指示書.md`
SHA256 `cbbd31db3c846465b232b80187444ec54c5a0242fbc449c5773db8b72d9cb758`
を今回の実施手順として採用した。前回のdirect-HID forensic/HIL候補手順は実行しない。
同指示書§25「captureがUSB trafficを取れていない」の停止条件を適用した。

次の既存資料を事前に読んだ:

- `altra-pairing-reconnect-acquisition-forensic.md`
- `altra-stable-identity-hil-results.md`
- `altra-stable-identity-offline-research.md`

Gate A未成立、NA DO NOT SEND、Run-7でfresh pair/ACK/N-burstなしという既存結論は維持。
今回、Bridgeからvendor commandを送っていない。Pair/Unpair操作、dongle抜き差し、
firmware/reset/reboot、DB/config/registry/startup設定変更、新規software installは行っていない。
production Backend / PairingController / ServiceRuntime / identity provider / Protocol / Bridge変更なし。

## Capture environment / actors

| Item | Observed state |
|---|---|
| Date / OS | 2026-10-09 JST; Windows 11 Home 10.0.26200 build 26200 |
| Dongle | official `0bb4:0350`; public alias DONGLE / DONGLE-BUS |
| Bus determination | Present-only PnP parent/rootとUSBPcap root inventoryの一致; 準備pcap descriptorでVID/PIDを確認 |
| USBPcap | existing `C:/Program Files/USBPcap/USBPcapCMD.exe`, 1.5.4.0; kernel driver RUNNING |
| tshark / Wireshark tooling | existing `C:/Program Files/Wireshark/tshark.exe`, 4.6.6 (v4.6.6-0-g3a22c3ef473d); existing capinfos使用 |
| SteamVR | initial/post-stop vrserver/vrmonitor不在; 起動・終了していない |
| Virtual Desktop | Streamer + service RUNNINGのまま; restart/killしていない |
| VIVE initial | VHConsole / ViveHubServer / ViveTrackerServer不在; VIVE Streaming Service STOPPED |
| VIVE normal launch | VHConsole通常launchによってViveHubServer / ViveTrackerServer起動; Hub表示2.5.6 |
| VIVE post-stop | 上記3 process RUNNING; VIVE Streaming ServiceはSTOPPEDのまま; ViveAgentService未起動 |
| Physical initial | 人間の回答: A/BともOFF、両充電USBなし、dongle接続済み |
| Physical operations in this run | B ON / OFF→ONを依頼・実施していない; A OFF維持; pairing gestureなし |

開始時、別の既存HIL由来の `vut_service.exe` を発見した。ユーザーが既存run終了を許可。
SIGINT handler / shutdown経路を静的に確認し、console memberが対象processだけであることを
確認してからCtrl+Cを送った。WaitForExit成功、direct-HID process不在を再確認した。
起動元の他repo/deploy/runファイルは変更していない。

広いprocess filterに一時的なPython processが含まれたため準備captureを停止した。
その後、command lineを含めたread-only再分類でtracker用Python/direct-HID actor不在を確認した。
準備pcapに公式runtime trafficを帰属させない。USB kernel/HID stackとcapture toolの
descriptor injectionを含むpre-runtime contextであり、唯一のofficial HID ownerを実証するcaptureではない。

公式runtime起動後にHID ownerのexclusive-open試験やhandle contention試験は行っていない。
Hubのstandby画面のみ観測し、dongle認識/Connected/Ready/trackingは未確認。
追加serviceを推測で起動していない。

| Official component | File version | SHA256 |
|---|---|---|
| ViveTrackerServer.exe | 1.1.4.28 | f459ecc348e7459ae6f3a4f3212e66433de7b1834c9d80b1776d3694d3fbe088 |
| vive_tracker_driver.dll | 1.0.7.98 | 49ed00b392b5b915aa71f3845d8b1b2d73f3192a7610d4781e8fb2b3fb910a09 |
| VHConsole.exe | 1.0.4.57 | 4ed906035752f52917ebd9e291c69b55245c6c77c01eaf9a1fb11743e5f4c0e3 |

## Capture outcomes

| Capture | Outcome |
|---|---|
| Preparation / bus validation | valid pcapng, 2,200 bytes, 28 packets, duration 13.496079 s; 22:30:24.435507–22:30:37.931586 JST; official runtime未起動、A/B OFF |
| Capture 1: official baseline | tsharkが `Capturing on ...` 表示後、extcap pipe format errorでexit 1; destination pcapngファイルは存在しない |
| Capture 2: B power-cycle | NOT RUN: Capture 1 success gate未成立 |
| Optional dongle reconnect | NOT RUN |

Capture 1のエラー原文:

```text
tshark: File type is neither a supported pcap nor pcapng format. (magic = 0x00000000)
Please report this to the developers of the program writing to the pipe.
```

tshark sessionが `Capturing on ...` を出した時点では開始できたと判断してHubを起動した。
次のsession確認で失敗を検出した。Hub起動receiptは22:32:30.2319104 JST（launch完了後の記録時刻）。
正常header/packet受信を確認してからruntimeを起動するhealth gateが不足していた。
この試行はvalid baselineとして扱わず、B ON依頼前に停止した。
原因は未特定。準備pcapが成功したことは、次のextcap pipeが正常だった証明にならない。
driver再起動、再install、追加capture、direct-HID代替を試していない。

Preparationのoffline解析:

- frame 2のdevice descriptorが `0bb4:0350` と一致。bus/topology/descriptorはprivateのみ。
- frame 11 / 25で64-byte interrupt INのRF statusを受信。
- RF statusをofficial SDK Connected状態、Bのbinding、NADS取得成功へ変換しない。
- USB control / other HID control / interrupt submission・completion・cancelがある。
- USBPcap descriptor injectionが含まれる。standard controlをvendor SetFeatureと混同しない。
- sanitized sequenceに28 frameを入力順のまま保存。RF words、raw IRP、payload、serialは出力しない。

**準備capture内のabsenceはofficial connect/reconnectでcommandが使われない証拠ではない。**

## Requested 3-way comparison: measured columns unavailable

Current Backend / guard列はresearch checkpointの静的資料に基づく。正常official captureの列は
未取得なので「未観測」とする。ゼロ件、不要、missing behavior確定とは扱わない。

| Stage | Official baseline | Official reconnect | Current Backend | Passive guard | Interpretation |
|---|---|---|---|---|---|
| Startup-only commands | 未観測 | 未観測 | normal startupでpair/PWSを送らない; pending pair-disable cleanupは別条件 | feature access抑制 | official startupとの差分未判定 |
| PairDevice | 未観測 | 未観測 | explicit pairing control + cleanupのみ | 抑制 | pair operationなしのofficial reconnectは未検証 |
| RxPowerSaving(false) | 未観測 | 未観測 | DeviceUnpaired時slot-local recovery | 抑制 | PWS/recovery不足の仮説が残る |
| RF/status query/init | 未観測 | 未観測 | RF statusからconfigureOnPairへ入らない | query/feature access抑制 | status-init必要性未判定 |
| Slot/post-pair init | 未観測 | 未観測 | DevicePaired → ARI1, ATM-1; host Wc/ATH1/AWH1/ANI0/ATM20; client ATH0/AWH0/ANI(slot)/ATM11 | configureTrackingOnPair=false + central guard | official connection時の対応条件未判定 |
| NA / info refresh | 未観測 | 未観測 | NA送信経路なし | 抑制; NAは禁止維持 | officialでNAが無いとは言えない |
| NANA→NADS→NASS→NASI→NAPI→NAV1→NARI→NAGN→NAZZ | 未観測 | 未観測 | 受信可能だがburst再送を保証しない | 受信のみ | preparation RF statusはN-burstではない |
| FW1 | 未観測 | 未観測 | NAZZ受信 → FW1 | 抑制 | official FW1の前後未判定 |
| FW2 / RestartRf / unknown | 未観測 | 未観測 | この調査で送信なし | 抑制 | vendor traffic context / 安全性は未検証 |
| First pose | 未観測 | 未観測 | existing pose decoder維持 | 受信のみ | official状態もpose開始順も未確認 |

NA observed from official runtime: **Unknown (no valid official trace)**。
Context / immediate response / state change: Unknown。Safety conclusion: DO NOT SEND。
PWS / PairDevice / post-pair init / FW1 / FW2 observed from official runtime: Unknown。
First pose relative order / reconnect-specific operations: Unknown。

## Acquisition judgment / preserved qualification

Most likely acquisition blocker: **F — まだ区別不能**。
今回の阻害要因はcapture transport失敗であり、tracker登録消失(E)やB接続不能を示さない。
BをONにしていないため、Bがofficial runtimeで接続失敗したという判定もしていない。
connectedだがserial再送だけ不足(A)、PWS recovery不足(B)、slot-init不足(C)、
vendor固有command依存(D)、registration消失(E)の比較に新しいofficial evidenceはない。

Safe reconnect sequence: **Unknown**。Safe device-info refresh: **Unknown**。
vendor runtimeがtrafficを送ったと観測できても、それだけでBridge任意送信を安全扱いしない。
今回のpreparation RF statusから安全sequenceを推定しない。

| Candidate | Stability | Acquisition |
|---|---|---|
| NADS | previous Class B / HIL_REQUIREDを維持; B evidence不足 | 未解決; fresh B baseline/reconnect未取得 |
| NASS | previous Class B / HIL_REQUIREDを維持; B evidence不足 | 未解決; fresh B baseline/reconnect未取得 |

Current Bridge missing behavior: 未確定。Passive guard impact: PWS・post-pair・FW1を抑制する
静的事実は維持するが、official successful reconnectとの因果差分は未観測。
Explicit Unpair: **NOT RUN**; verified wire unpair path未確認、PairDevice disableと混同しない。

## Evidence / reproducibility / privacy

raw保存先はgitignored `hil-private/official-runtime-usbpcap/`。
準備pcap、PnP/root inventory、CSV、process/service snapshot、operator receiptはprivate。

Preparation pcap SHA256:
`5bc860134e44906eaa51f57c5deba79799ce2705e89a4d54f8de17bf895d2d78`。
Capture 1 SHA256: **N/A (file not created)**。Capture 2 SHA256: **N/A (not run)**。
rawはcommitしない。public JSONはpreparationとfailed/not-run official captureを明確に分離。

Research parser `scripts/sanitize_altra_usb_sequence.py` はoffline CSVだけを読む。
HID Set/GetFeatureはclass/interface request + feature report typeから判定。
標準USB control/descriptorをvendor command扱いしない。入力順を保存し、同じframeの重複のみ除去。
request/response frameは保持し、IRP pointer再利用をcommand再送回数と混同しない。
unknown payload、serial、RF、raw pointer、topology、絶対packet時刻は出力しない。
ひとつのinventoried dongleへfilterしたCSVだけを受理し、複数targetをrejectする。
NA/FW等のsynthetic unit fixtureはclassifier検証であり、実機送信・観測ではない。

CSV抽出に使用したinstalled tshark fields（private outputにのみ保存）:

```text
frame.number frame.time_epoch usb.bus_id usb.device_address usb.irp_id
usb.transfer_type usb.endpoint_address usb.control_stage usb.bmRequestType
usb.setup.bRequest usbhid.setup.bRequest usb.setup.wValue usbhid.setup.wValue
usb.setup.wLength usb.data_len usb.capdata usbhid.data usbhid.data.vendor
usb.request_in usb.usbd_status usbhid.data.report_id
```

`tshark -r <private-pcap> -T fields -E header=y -E separator=, -E quote=d`
と各fieldの `-e` でoffline抽出した。raw CSVはprivate。

```text
python scripts/sanitize_altra_usb_sequence.py --input <private-csv> --output <sanitized-json>
python -m unittest discover -s tests -p test_altra_usb_sanitizer.py -v
```

Validation: parser 8 unit tests PASS (malformed input, unknown, known commands,
ordering, exact duplicate/conflict, completion correlation, identity redaction, multiple-target reject)。
実preparation CSV 28 rows解析PASS。既存6 CTestは5 PASS + offline TEMP permission error;
同offline testを通常TEMP権限で再実行してPASS。real private serial/RF leakage scan PASS (7 identifiers / 33 representations; full preflight payload scan含む)。
HIL normal connect/reconnect / hardware qualificationはPASSではない。

## Next observation

今回の停止条件後に追加captureを行わない。次回は既存USBPcap/tsharkのcapture transportを先に
read-only inventory + passive preflightで確認する。extcap pipeとUSBPcap direct-to-fileのどちらを
使う場合も、出力file生成、valid header、実USB packet受信、VID/PID一致を確認してから
official runtimeを起動する。前回成功fileだけを今回のhealth proofに使わない。
原因切り分けにnew installや設定変更が必要ならそこで停止する。

health gateが成立して初めて同じ登録状態のB-only normal connectionを観測する。
Connected/Ready/trackingが確認できた場合だけB OFF→ONへ進む。
Pair new / Unpair / firmware / manual Bridge commandには進まない。

Research branchへsanitized report / sequence / parser / testsのみcommit/push。
integration branchへmergeしない。
