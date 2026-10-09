# Altra official runtime USBPcap: capture transport stop

## Latest: direct-to-file Health Gate, 2026-10-09 JST

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
