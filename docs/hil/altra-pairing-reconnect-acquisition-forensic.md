# Altra pairing / reconnect / device-info acquisition forensic audit

調査日: 2026-10-09 JST。Source of Truthはユーザー指定
`MonakaVR_Altra_Pairing_Reconnect_Acquisition_Forensic_Codex指示書.md`。
開始checkpoint: `8757758fb6583a745bd1b28886487eb4b516b9c3`、
作業branch: `research/altra-stable-identity-hil`。
prepared lineage `a594e4377ef2b5cca04e915187b0c12e59064839` からのancestry確認済み。
主checkoutのfeature branchと未追跡attachmentを保持した。
production Backend / PairingController / ServiceRuntime / StableIdentity provider、
MonakaProtocol / MonakaBridgeは今回変更していない。

## 判断

**acquisition blockerは一種類ではない。** 保存capture再起動のHIL-02と、
disconnect後のHIL-03、最新B-onlyのRun-7を分離する必要がある。

- HIL-02ではfresh pair / NADS / NASSがなくても、slot0/4のRF診断値は
  接続時に観測された `0x32000008` を維持した。captureはposeを保存しないため、
  現在のpose受信や公式SDK Connectedを証明できない。再送trigger不足の仮説を残す。
- HIL-03ではAPF / disconnect後に対象slotの診断値が
  `0x320fa008` へ変化し、そのまま停滞した。過去の限定slot0 HILでは、
  この値から `RxPowerSaving(false)` により `0x0a000005`、通常power-on後に
  `0x32000008` / pairへ戻ったと記録されている。
  passive guardがその復帰操作を抑制していることはコードで確定。
  それが今回の失敗の原因であるという因果関係は未検証。
- 最新B-only Run-7は19本すべてRF status reportで、pair / ACKはゼロ。
  generation 1と11のslot診断値が同じで、成功例の `0x32000008` への遷移がない。
  Bの現在RF/slotもbindingできていない。**正常接続済みでserialだけ不足とは言えない。**

外部実装のpost-pair設定はBridgeにも存在する。ただしpair eventを受けてから
実行されるため、pair event自体がない最新B-only試験を、それらの抑制だけで説明する
ことはできない。startup側RF設定、既存registration、runtime側接続完了処理、
statusからの再初期化が引き続き切り分け対象となる。

**実機HIL: NOT RUN。Gate A未成立。** 既知commandの存在と安全なsequence全体の
証明を混同しない。raw NA、RestartRf、reset/reboot、firmware操作は送信していない。
デバイスopenも今回行っていない。安全な次の受動観測を以下に具体化して終了する。

## 証拠の区別と再現資料

| 種別 | 意味 |
| --- | --- |
| CODE | 固定commitの送信/受信コード。実機効果は保証しない |
| API | 公式SDKの公開APIとstate定義。Windows HID wireとの対応は保証しない |
| STATIC-BINARY | 公式配布/既存installed binaryのhashとstrings。dispatchの証明ではない |
| SAVED-RAW | 既存保存HID reportの今回のoffline再解析。新規HILではない |
| HIL-HIST | 過去branchにあるHIL結果。今回再現しておらず、元pcap未再検証 |
| CANDIDATE | 根拠はあるが因果関係/安全性/適用範囲が不足 |

再現用の自作read-only工具:
[`audit_altra_forensic.py`](../../scripts/audit_altra_forensic.py)。
Git objects、saved NDJSON、binary bytesのみを読む。外部コードやbinaryを実行しない。
出力はhash、line index、allowlist token有無、opaque RF status wordsに限定し、
serial、RF address、自由文、第三者ソースを出力しない。

- [local provenance](altra-pairing-reconnect-acquisition-local-provenance.json):
  取得済み全22 refs、7種の関連source path、関連変更16 commits、
  公式SDK/installed runtime 4 binariesのhashとtoken有無。
- [external sources](altra-pairing-reconnect-acquisition-sources.json):
  固定commit / source blob / runtime前提 / 分類。
- [saved evidence](altra-pairing-reconnect-acquisition-evidence.json):
  7本のoffline identity分析とRF status再解析。run aliasは同一入力順。
  raw bytesや完全serialは含まない。

全local heads / fetch後のorigin refsと、到達可能な関連変更履歴を調べた。
削除済み・非公開・取得不能なbranchが存在しないとは主張しない。
`git fetch --no-prune origin` で新たに
`diagnostic/runtime-restart-observability` も取得して比較した。

## 外部repo / 公式reference inventory

下表のHIL欄は**今回の実行**。sourceに実機動作の記述があっても独立HIL receiptと同一視しない。
短縮commitはsources JSONの完全SHAに対応する。各repoのsourceはread-onlyで調査し、コピーしていない。

| Repo / branch / commit | Language / host前提 / dongle VID:PID | Pair / reconnect | Device-info / risk | 分類・confidence / HIL |
| --- | --- | --- | --- | --- |
| shinyquagsire23/vive_ultimate_tracker_re / main / 11ae3cbf | Python hid・toybox / 0bb4:0350 | constructorでpair enable、pair入力後にrole/host/mode設定。re-pair再init TODO。PWS/RestartRf例はcomment | N-category受信、NAZZ後FW1。clientにmap resetも送る。NAは疑問付きenum | Historical reverse engineering。CODE高、sequence必須性低 / NOT RUN |
| nijkah/pyvut / main / fcfcd33f | Python・Ubuntu、既存mapはWindows vendor環境で準備 / 0bb4:0350 | 旧toybox相当のstartup pair、first tracker host、client mode。独立reconnect証明なし | N受信、NAZZ後FW1。client map resetを含む。flash/restart blacklistあり | RE derivative。CODE高、HIL効果未確認 / NOT RUN |
| h228zhou/pyvut / main / 7b3a7ad1 | Python・同系fork / 0bb4:0350（importするenum） | pair_on_startup既定false。RF statusのpaired bitでもinitialize_slot、再接続時slot再init | **実際のNA senderあり**。post-initと欠落SN時のpose/map経路で要求。safety traceなし | Experimental derivative。CODE高、NA非破壊性低 / NOT RUN |
| mscheibl/pyvut / main / fcfcd33f | Python・nijkah同系 / 0bb4:0350 | inspected tracker_coreはnijkahと全文同一 | 同一sourceなので独立 corroborationにならない | Historical/derivative / NOT RUN |
| HukoOo/pyvut / main / fe38eac5 | Python・同系 / 0bb4:0350 | inspected tracker_coreはnijkahと全文同一 | README変更。新しいhandshake証拠なし | Historical/derivative / NOT RUN |
| mgschwan/ViveUltimateTrackerMobile / main / 79e8d3fb | Java/Kotlin・Android USB host、phone/Quest / 0bb4:0350（ConnectionFragmentで2996/848） | InitDongleでpair enable。master先行。pair後FW2とmode21、client mode11。READMEは自動error recoveryなし | NAZZ後FW1、client map reset。masterと後続map上書きの注意。NA sender未発見 | Experimental・RE由来コード共通部分あり。差分は実在、一般化不可 / NOT RUN |
| ViveRoboticsSoftware/vive-ultimate-tracker-sdk / main / 86851127 | C++ API、Ubuntu/Linux server・Unix-domain IPC、proprietary .so / 公開APIでVID:PID特定不可 | Pairing/Unpairを明示。ConnectingとConnectedを分離。wire sequence source非公開 | Add(id,name)のnameとNADS/NASS対応不明。CommandResponseは変更前state | Verified / official API。API高、wire safety不明 / NOT RUN |
| orzkwsk/MonakaVR-HIL / main / sources JSON参照 | PowerShell/Python、pin済みsource群のHIL infrastructure / N/A | README・source-repos manifest・treeにvendor handshake実装なし | raw evidenceはlocal-only方針。今回新規runなし | Independent infrastructure、pairing根拠なし / NOT RUN |
| Installed VIVE Ultimate Tracker runtime / file version・hash固定 | Windows、vive_tracker_driver.dll 1.0.7.98 / binary textだけではVID:PID確認不可 | 接続/初期化/storage削除に関連する診断名が存在 | NADS/NASS/NAZZ/FW1あり。stringsだけでwire/副作用は未確認 | Official installed binary。静的存在高、dispatch不明 / NOT RUN |

primary repositoryのhistory/diagnostic branchesは次節とlocal-provenance JSONに全SHAを記録。
ownerのrepository一覧、public repo検索、MonakaVR scoped code検索も実施した。
Pico4Ultra_ResearchToolsのtreeはPico対象で、Altra wireの証拠として採用しない。
見つからないrepo/fileが存在しないと断定するものではない。

固定sourceリンク:

- [original toybox](https://github.com/shinyquagsire23/vive_ultimate_tracker_re/blob/11ae3cbfd9a331694c72890df7dced77248b4ba6/tracker_toybox/tracker_toybox.py):
  startup 156–181、pair 383–431、NAZZ処理804–816。
- [nijkah tracker_core](https://github.com/nijkah/pyvut/blob/fcfcd33f4c1f16b0d84f5f741dc1319abdc7942a/pyvut/tracker_core.py):
  startup184–209、post-pair432–460、NAZZ865–877。
- [h228 tracker_core](https://github.com/h228zhou/pyvut/blob/7b3a7ad16b0c1b0a62afe7bef743fdb8094a67d9/pyvut/tracker_core.py):
  NA sender162–165、status再init268–339、追加要求841–856。
- [Android HidBridge](https://github.com/mgschwan/ViveUltimateTrackerMobile/blob/79e8d3fb71cb34187d369596aefe2aabc195c318/android/app/src/main/java/io/xrworkout/htctrackerdongle/HidBridge.java):
  NAZZ917–930、pair1295–1341、InitDongle1392–1409。
- [HTC header](https://github.com/ViveRoboticsSoftware/vive-ultimate-tracker-sdk/blob/8685112780734c2513c66d53b9eb104db47afcf9/include/ultimate_tracker.h)
  と [SDK lifecycle / command guide](https://github.com/ViveRoboticsSoftware/vive-ultimate-tracker-sdk/blob/8685112780734c2513c66d53b9eb104db47afcf9/VIVE-Ultimate-Tracker-SDK-Guide-EN.md)。

## 過去branchと現行実装の比較

| Ref / checkpoint | 確認事項 | 今回の評価 |
| --- | --- | --- |
| dev/vivehub-bypass / db7c6795 | startup PairDeviceとconfigureOnPairの歴史的起点 | 旧起点、startup enableを正常reconnectの正解として復活させない |
| experiment/vive-auto-reconnect / 24228627 | startup / runtime RestartRf、RF status parserとHIL docs | runtime cold payload再利用はclean失敗記録。slot-specificは試験手順と一部selector観測で、完成済みreconnect証明ではない |
| experiment/vive-debug-console / 1e929036 | 複数policy/raw送信面、限定PWS-off HIL記録 | known disconnect後slot0のsingle-action recoveryは候補。console全体は未知/禁止操作を含むので起動しない |
| experiment/vive-test-harness / 7ee46d6c | diagnostic harness、handoff、reconnect履歴 | 古い挙動・stale binary/汚染証跡を維持。runnerの存在をHIL成功としない |
| diagnostic/vive-slam-recovery-controls / c8a20308 | 通常PWS/NAZZ維持、追加SLAM controls | manual query/reset面を採用しない |
| diagnostic/runtime-restart-observability / 36f4990f | opt-in runtime/provenance、通常PWS/NAZZ維持 | read-only比較。新しいserial query/verified unpairは見つからない |
| refactor・fix・design・control-model・service-owner系 | shared production backend blob 37b674f9… | HID command policyは同系。ServiceRuntimeを新しいvendor handshakeの根拠としない |
| feature/altra-pairing-assignment-ux local/remote | backend blob 4f833616…、remote/local SHA差も保持 | cleanup改善前の歴史も区別 |
| research / 8757758 | backend blob a28f5674… | production baseとの差は既存passive guard / callback。今回その既存差分も変更しない |
| main / 5fc42af3 | src/backend.cppなし | prepared lineage外の実装起点にしない |

`dd4205f1ac4af1fbbdd49328d55a0bd2ece286cc` のPWS-off policy追加と、
debug-consoleの2026-09-19記録はslot0で以下を述べる:
接続値→power OFF/disconnect→PWS-off→reconnect-ready値→通常ON→pair。
元USBPcapを今回取得できておらずHIL-HISTに限定する。
「5回繰返す」の記述は**procedure**であり、5回PASSのreceiptではない。

`d9223ad9077aaafaa90e5e6aebe160c67c83be79` はstartupにもPWS-offを追加したが、
`937bd9be4e8266ba369a8d5795c6521784e50783` はpairingとの干渉と検証範囲を理由に
startup送信を撤回して観測disconnect後だけに限定した。
`07fbef4dd1367bfe970bb58dc3bb86ae0e4178fd` は通常startupのpair enableを撤回。
したがって「startupでPWS/pairを追加すれば安全」とは結論しない。

過去成功capture `vive_init2.pcapng` のcommitted reportには、Backendとは別の
controllerから遅延したFW1/ANI0/ATW/ACF60/ATS…/country/FW2等が記載される。
独立actorの初期化が**そのrunにあった**証拠だが、恒常的vendor依存の証明ではない。
clean `vive_init3.pcapng` のruntime cold-RestartRf失敗を打ち消さない。
この二つの元pcapは今回利用したlocal証跡群/Downloads直下にはなく、再decodeしていない。

## 公式SDK semantics

公式stateはregistration、接続、初期化、map準備を別段階としている。
`Connected` はtracker dataの受信を指し、`Initialized` はtracking開始前。
公開subscriberに見えるのはSyncing / BuildMap / Readyなので、
**Addが来ないことからRF未接続とは言えない**。
Add(id,name)と無線serial namespaceの対応は未確認。
Removeはheaderでtimeoutに伴うlist removalとして説明され、explicit Unpair完了と同一ではない。

PairingとUnpairはid-list必須のAPI。Restartはdisconnect/restart、Setupはmap構築であり、
今回は実行しない。CommandResponseはcommand実行による変化の前のstateで、
成功文字列や最初のcallbackだけを解除完了証拠にしない。
ガイドにあるarbitrary state問い合わせも**SDK serverへのAPI**であり、
同じ文字列をHIDへ送る根拠ではない。
[公式headerとguide](https://github.com/ViveRoboticsSoftware/vive-ultimate-tracker-sdk/tree/8685112780734c2513c66d53b9eb104db47afcf9)。

[公式LED表](https://www.vive.com/ca/support/ultimate-tracker/category_howto/status-led.html)
ではblueは接続中、breathing blueはstandby。最新BのLED観測はそれに整合するが、
registration喪失、破損、SDK Connectedの厳密な判定はできない。

## 再構成したsequence / current-passive差分 matrix

「mandatory」は既存API/実装の条件に限定する。vendor wire全体の必須順序は未確定。

| Phase | Current Bridge (8757758 / production base) | original RE / nijkah | h228 / Android | Official SDK/runtime | Passive capture / confidence |
| --- | --- | --- | --- | --- | --- |
| Host startup / dongle open | Windows SetupAPI/HID。通常startupにpair/PWS送信なし | HID interface0、dongle自体のID/version query | h228はpair既定off、Android USB permission/claim | Linux server lifecycle。Windows binaryにHID初期化の診断名 | exclusive openだけ。openは実装上mandatory、今回実機NOT RUN |
| Dongle initialization | normal startupはopen、pending pair-disableのみ条件付き処理 | ID query後PairDevice enable、先頭3selectors | h228は条件付きenable、Android enableは同じ先頭3selectors | exact wire init非公開 | 全feature exchange禁止。queryが接続必須かUnknown |
| RF / power-saving | 実際のdisconnect後に対象slot PWS-off | RxPowerSaving例はcomment | Android復帰なし、h228 status再initあり | wire非公開 | PWS-off抑制。HIL-HISTはslot0のみ、Bへの適用未検証 |
| Pair enable / physical gesture | 明示beginPairingのみ、選択slotまたはall | startup enable、disable lifecycle不明 | h228明示opt-in、Android init enable | Pairingと物理button | block。Pairing API内のgestureはmandatory、通常reconnectへのenableは未承認 |
| RF association / DevicePaired | 0x18 decode、slot/address保持 | 同系decode | statusから再initするh228差分 | PairedとConnectedは別段階 | 5成功connect/burstでobserved。registration新設/完了の証明ではない |
| Post-pair host configuration | ARI1→ATM-1→host Wc/ATH1/AWH1/ANI0/ATM20。client ATH0/AWH0/ANI(slot)/ATM11 | 同系のhost20/client11 | h228同系+NA。Android FW2/mode21とclient map処理差分 | Connected→Initialized内部非公開 | config flag off、central guardでもblock。実装上存在、必須性Unknown |
| Existing-session rediscovery | pair eventのconfigureOnPair。poseからstateは更新するがRF-statusからconfigureしない | 再pair TODO / 保証なし | h228はpaired-statusでもinitialize_slot | API state tableはあるがwire差分不明 | statusはraw保存のみ。missing-event init候補、bit意味は推測しない |
| Connection establishment | 完了stateを直接検証しない | host/client/maps管理。共通REなので独立検証とは数えない | Android master先行。mode20と21/FW1と2を同一視しない | Connected/Initialized別状態 | passiveをnormal backendと同等に扱えない。Partial |
| Nine device-info ACKs | receive ACKとして扱う。query実装なし | N受信、serial名enum | h228 NADS保持+NA要求、Android N受信 | SDK name correlation不明 | 5 bursts observed。必須membership、trigger因果、deadlineはUnknown |
| Host reply / FW1 | NAZZ受信後にFW1送る | 同系。client map resetを含む | Android NAZZ→FW1、h228同系 | native FW1 stringあり、dispatch未確認 | FW1も抑制。**先行NADS/NASSの必要条件ではない**（保存成功例） |
| Pose stream | decode/snapshot/publishは別経路 | SLAM/map/pose管理 | client map / FW2等追加差分 | subscriber stateはmap準備後 | poseはraw/eventとも保存しない。first pose orderはUnknown |

保存成功例の観測順:
`DevicePaired → NANA → NADS → NASS → NASI → NAPI → NAV1 → NARI → NAGN → NAZZ`。
pair→NANAは約320–376ms、NANA→NAZZは約288–304msのhost receive区間。
保証timeoutでもtracker clockでもない。
post-pair host ACK / FW1はこのpassive recorderから送信されていない。
したがって、それらがこの5例のN-burstに必須だったという主張は反証される。
他actorの過去設定やwire状態の影響を排除した全環境の一般命題にはしない。

## 保存RF evidenceによる失敗分類

historical parserの0x1d/0x1e layoutを研究tool内で読み、5個の32-bit little-endian wordsを
そのまま表示した。bit enum、SDK state、physical Bのslotには変換しない。
optional report ID / 宣言length / inner lengthを検査した。
7本計204 RF reports、malformed 0。詳しいgenerationとraw indexはevidence JSON。

| Run | Scenario | RF reports | opaque wordsの変化 / 併存観測 |
| --- | --- | ---: | --- |
| 1 | HIL-01 | 35 | slot0/4が0x0a000005→0x32000008。A/B connectとburstあり |
| 2 | HIL-02 restart | 9 | slot0/4は0x32000008のまま。fresh pair/ACKなし |
| 3 | HIL-03 power cycles | 53 | slot0、後にslot4が0x32000008→0x320fa008。APF/disconnectのみ |
| 4 | HIL-04 dongle reopen | 8 | generation1のslot0/4停滞値からgeneration8で0x0a000005。fresh burstなし |
| 5 | HIL-05 B-first/A-second | 21 | slot4、後にslot0が0x0a000005→0x32000008。fresh bursts |
| 6 | HIL-06-after A | 59 | gen1はslot1/3が0x03000005。gen9で0x0a000005、slot3のみ0x32000008へ。A bindingあり |
| 7 | resumed B-only | 19 | gen1/11とも [0x01000003,0x0a000005,0x01000003,0x0a000005,0x01000003]。変化/pair/ACKなし、B slot未binding |

Run-7の列位置を以前のHub cacheやBの旧RFで補わない。
NADS/NASSは全7本再解析でも各Class B / acquisition UNRESOLVED。
5成功bursts以外にfresh identityを作っていない。
HIL-02等のraw index gapsからpose存在を推定していない。

## 仮説A–Eのsupport / contradiction

| Hypothesis | Support | Contradiction / unresolved | 判断 |
| --- | --- | --- | --- |
| A: 正常接続済み・N再送だけ不足 | Run-2の接続時と同じdiagnostic words。h228にrefresh sender | Run-3/7にはconnectに向かう証拠不足。first pose不明。NA安全性未証明 | restartケースのCandidate。最新Bの唯一原因とは言えない |
| B: post-pair/reconnect host sequence不足 | Bridgeが0x18のみでconfigure。h228はstatusからもinit。AndroidにFW2/mode21差分 | passive成功5例はconfigure/FW1なしでburst。最新Bにはconfigureのtriggerとなるpairもなし | reconnection / startup完了の不足Candidate。post-pair単独原因は未証明 |
| C: vendor runtime初期化依存 | historical混在pcap reportに別actorの遅延sequence。Windows driverにdevice-info/connection初期化診断名 | Ubuntu/Android実装はstored map前提で独自動作を主張。これを今のBで検証していない | vendor機能の存在はsupport、必須のrunning dependencyはUnknown |
| D: passive guardが必要操作を抑制 | コードとfake testsでPWS-off/configure/FW1全抑制。Run-3停滞値とPWS history整合 | normal Backendとの同条件比較なし。Run-7にdisconnect入力すらなし | **抑制はVerified**。接続失敗の因果はCandidate、特にRun-3 |
| E: registration不完全/破損・explicit unpair必要 | official SDKにUnpair。過去Hub launch後cache変化/re-pair報告 | cache、APF、LED、DeviceUnpairedは削除証拠でない。wire delete/responseなし | Unknown。unpairが必要と決めず、API候補と検証方法のみ |

## Command risk / HIL admission table

「条件付き」は今回送信したという意味ではない。**本taskは全件NO SEND**。

| Command / path | Known sender / known effect | Risk / expected response | HILへの扱い / evidence |
| --- | --- | --- | --- |
| 0x1d behavior0 PairDevice enable/disable + 5 selectors | current begin/endPairing、RE/Android enable | receiver behaviorを変える。feature echoはsemantic successではない。disableは登録解除でない | 明示pair lifecycleのCandidate。B physical binding/cleanup/sequenceを確定するまで送らない |
| 0x1d behavior1 RxPowerSaving(false), one observed slot | current disconnect recovery、slot0 HIL-HIST | 既知限定復帰: stuck word→reconnect-ready→通常ON pair。startup/allへ一般化するとpairing干渉 | Gate B/Cの候補、Gate A/適用slotを満たさず今回NO SEND |
| TX 0x18 ACK ARI1 / ATM-1 / ATH / AWH / ANI / Wc / ATM20・11 | current configureOnPair、original/nijkah | role/host/Wi-Fi/mode変更。単なる読取queryではない。現在は各送信結果を接続完了判定しない | CODE known、sequence全体Candidate。NAZZ/pose等を別途観測する必要 |
| TX FW1 after NAZZ | current/RE/Android | firmware-mode関連の意味が完全には未確認。firmware image writeという根拠はないが、mode変更を無害と仮定しない | 既知reply Candidate。先行serial refreshではない。今回NO SEND |
| Android FW2 / ATM21 | Android pair path | current host20との差分。本文commentも意味未確定 | 複数実装の一致/公式wire対応不足。NO SEND |
| NA | h228実sender、他REは疑問付きenum | device-info resendの作者主張。sole effect/非破壊性/firmware dispatch/trace未証明 | **Unknown / DO NOT SEND** |
| ADS / ASS / NADS / NASS文字列をqueryとして送る | 受信namespace定義のみ | requestとしてのopcode/意味なし | unknown injection、NO SEND |
| RF status 0x1e query / dongle ID/version query | historical debug/RE送信例 | receipt読みとrequest送信は異なる。dongle SNはtracker SNでない | 今回は保存/自然入力のみ。不要な実機queryをしない |
| RestartRf behavior2 / Restart API | experimental/SDK | reset/restart分類。clean失敗history、selector推定あり | ユーザー禁止・Gate C不成立、NO SEND |
| behavior5 factory reset / behavior6 clear-pairing candidate | RE comments | persistence破壊の可能性、unpair wireの検証なし | NO SEND。official Unpairと対応させない |
| DCMD_21・flash/erase/DFU/write CR ID / map reset P系 | RE blacklist、client map処理、SDK Setup | brick/reset/persistent変更の報告または意味未確定 | NO SEND。外部appを丸ごと実行しない |
| SDK Unpair <one id> | official API | documented single-selection API。ただしresponseはpre-change、wireとtracker-side結果未検証 | API exists Verified、完了手順はCandidate。current B id不明、Linux server未起動 |

TX 0x18はfeature commandで、同じ値のinput pair reportとは方向/formatが異なる。
current sendAckはpacket生成後のcommand exchange結果を戻り値へ反映しない。
begin/endPairingもmatching command responseの観測であり、RF登録処理やdevice側成功まで
確認したものではない。これを修正したとは主張せず、今後の診断要件として残す。

## NA再監査: sender発見と未解決の安全性

以前のoffline reportの「sender-side implementationなし」は**調査範囲内の旧結果**。
今回はh228 forkに実senderを発見したのでその部分を更新する。

[`0170c51676b5429f83235dcbbf650af7e72f28f2`](https://github.com/h228zhou/pyvut/commit/0170c51676b5429f83235dcbbf650af7e72f28f2)
がrequest_device_infoを追加し、post-initとSN欠落時にNAを送る。
その前の
[`67dfff8dfddfa2971614da95c21964bd3e31e4a8`](https://github.com/h228zhou/pyvut/commit/67dfff8dfddfa2971614da95c21964bd3e31e4a8)
はstatus-triggered initializationとstartup pair既定offを導入している。
これらは別変更で、post-init不足とinfo refresh不足を一括して証明するものではない。

| 問い | 今回の結果 |
| --- | --- |
| 実際に送るpublic sourceはあるか | **Yes: h228 request_device_info / send_ack_to** |
| 送信前後のraw trace、firmware/version、sole effectを検証できるか | No。pinのtreeには当該HIL trace/pcapなし。READMEはrepo外testへの参照、raw receiptなし |
| 元RE/nijkah/mobileも送るか | 調べた関連pathsではenum/commentのみ。copy数を安全性corroborationにしない |
| Native stringsでdispatchを証明したか | No。SDK .soにexact NA stringがあるがSDK Unpairedのaliasでもあり得る。xrefs/送信経路は未確定 |
| Installed driverではどうか | NADS/NASS/NAZZ/FW1あり、exact NAなし。このabsenceもruntimeがNAを送らない証明ではない |
| firmwareのreceive-side処理を検証したか | No。firmware extraction/実行/書込みなし |
| 結論 | **NA safety Unknown。DO NOT SENDを維持** |

公式SDK binary:
`bin/linux/x64/libultimate-tracker.so` を固定commitからTEMPに取得し、実行せず調べた。
416632 bytes、
SHA256 `ac34cd0c5805b4ff55d8db805930e3f940f7f97285bd8155a0bd64f1a08950ce`。
Pairing/Unpair/FW1やpairing/connecting/rf_connected関連symbolは静的に存在するが、
wire dispatchや安全性はそこから証明しない。
Windows driverにはTrackerDongleHIDのstorage削除、AddTracker、HIDManager device-info取得等の
診断名がある。これも**候補入口**であり、API↔wire↔tracker効果の対応を確定していない。

## Explicit unpairの分離

**official APIとしてUnpairは存在。verified completion procedureはまだない。**

| 項目 | Established / missing |
| --- | --- |
| Operation | official SendCommandのone-id Unpair。PCのlist削除と区別 |
| Wire command | 非公開native implementation。PairDevice disableやbehavior6と対応付けない |
| Response | API callbackのResultとpre-change state。これだけで完了判定不可 |
| Later state transition | 同じtargetで後続stateを記録する必要。Unpairedが一時disconnectと異なることを検証 |
| Tracker-side behavior | 通常OFF→ONで再接続できないこと、registration absenceの複数面確認が未実施 |
| Re-pair requirement | 明示Pairing + gestureをしないと復帰せず、その後fresh identity取得することが未実施 |
| Current Bridge | explicit delete-registration APIなし。DeviceUnpaired受信→PWS復帰は別機能 |
| Hub UI | 過去Pair newは実施報告あり。single-target removal操作/responseの証跡なし |

将来のsingle-B確認は、B以外OFF、fresh bindingしたtarget idだけを対象にする。
通常disconnect/power-offをbaselineとして記録後、verified official API操作、
後続state table、通常wake時の接続可否、明示re-pair requirement、
fresh NADS/NASSと旧B exact bytes比較を別々に残す。
長時間ack欠落だけでregistration absenceを証明しない。cache削除やall-target操作で
結果を代用しない。今のB target idが不明なので実施していない。

## PairingControllerの評価

`observe` はoperation window、generation、selector、valid slotを検査してDevicePairedを採用。
`tick` はその後PairDevice disableを確認してPaired resultとする。
configureOnPairはBackendのpair入力処理でcontroller callbackより先に走る。
cleanupはbounded retry、transport reopen時のpending disableを保持する。
それらのsoftware lifecycleは既存mock testsで検証できる。

ただしPaired resultはDevicePaired観測とpair-disable確認であり、
**Connected / Initialized / N-burst / pose開始は条件ではない**。
既存trackerのreconnectがwindow内に来る場合も新しいregistrationを作った証明にならない。
またpost-pair ACK個々のsemantic acceptanceを確認しない。
したがってend-to-endの正常接続に対するcorrectnessは**Unknown**。
現行を正解にして補完したり、unsafeと断定してproduction変更したりはしない。

## PC runtime / HID ownership

今回のread-only snapshot（2026-10-09 JST、HIL preflightではない）:

| Actor | 状態 / 今回の操作 |
| --- | --- |
| SteamVR vrserver / vrmonitor / vrcompositor | inspected process listでなし。終了/再起動していない |
| VirtualDesktop.Streamer.exe / VirtualDesktop.Service.exe | running（PID2148/5600）、service Running。変更していない |
| VIVE Hub / ViveHubServer / ViveTrackerServer / ViveAgentService | inspected process listでなし |
| VIVE Streaming Service | Stopped。既存明示許可の停止状態を維持。PathNameの配置のみ読取 |
| research capture / backend | 起動していない |
| USBPcap driver / USBPcapCMD / tshark | 既存driver Running、既存toolsあり。capture未開始 |
| Actual HID owner | **Unknown / NOT INSPECTED AT HANDLE LEVEL**。今回exclusive openを試していない |

前回sharing violationのownerは未解決のまま。process absenceやVD存在だけでownerとしない。
handle.exeはPATHで見つからず、任意場所の全探索や新規installはしていない。
次に競合が出れば、read-only handle inventory（既存Process Explorer等、必要なら公式
Microsoft/Sysinternals工具）、PIDとHID object/device pathの照合を先に行う。
Restart Managerのresource登録でHID handleを必ず列挙できるとは仮定しない。
不明のままkillせず停止する。SteamVR/VDの正常終了・再起動許可はあるが、
Gate Aが未成立なので状態を変えて原因を混ぜる必要はなかった。

## Gate判定と次の安全な観測

| Gate | 判定 |
| --- | --- |
| A: 2 independent sources または official+implementationで正常sequence裏付け | **NOT MET**。RE/forks/Androidには共通由来があり、official APIはraw host sequenceを公開しない。mode/FW差分の必須性も未解決 |
| B: 予定commandが既知/official/複数実装で非破壊 | pair/PWS/configure/FW1は限定候補。NA非破壊性は未成立、unpairのwire/完了も未検証 |
| C: flash/write/reset/reboot/unknown以外 | RestartRf/firmware/reset系を除外。NAもuser禁止。外部app全体は許可できない |
| D: purposeとexpected response | PWS-after-observed-disconnectは限定historyにexpected transitionあり。ただし最新Bはfresh target bindingなし |
| Stop conditions | 最新Bのphysical RF binding不明、sequence根拠不足。今回HIL-A/B/CすべてNOT RUN |

次の優先観測は**official normal connectionのUSBPcap受動trace**。
direct exclusive captureとofficial runtimeを同時にdongle ownerにしない。
準備は既存USBPcap/tsharkで可能だが、今回は開始していない。

1. Bのみのphysical状態を改めてhuman確認し、A OFF、charging USB除去を記録。
   PCのprocess/service snapshotとowner evidenceを保存する。現在状態を古いreceiptで代用しない。
2. direct-HID Backend/recorderを閉じた状態でUSB bus-level passive captureを先に開始。
   ordinary official runtimeの既存registration wakeをまず観測する。
   firmware updater、reset/reboot、map Setupには進まない。
3. SetReport/GetReportのdirection、report type、interface、payload、response、
   0x18 input、RF words、N-burst、first poseのtimestampを同じclockで採取。
   HID feature echoとRF/device側ACKを別recordにする。process帰属はUSBPcap単独で断定しない。
4. ordinary wakeで接続しない場合はそこで保存して停止。
   registrationを消す操作や未知NA送信を追加しない。
   normal official Pairingの別runが必要なら、それを明示して時間軸を分ける。
5. traceから接続前/0x18後/NAZZ後のhost処理を抽出し、
   current Bridge、Android FW2/mode21、h228 status-triggered initと照合する。
   officialがNAを送ってもsole effect/非破壊性の証明なしにdirect送信へ移さない。
6. safe sequenceを再レビューしGate A–Dが揃った場合だけ、
   B単独のresearch-only allowlist診断を設計する。
   最新RF binding、bounded cleanup、exact command/response receipt、
   pose開始count/time（raw pose全保存は不要）を含める。
   production guardは解除しない。

この観測でAは「data/poseがあるのにN不足」、Bは「association後init未完了」、
Cは「official actorだけが追加operationを送る」、Dは「currentの既知single-actionを
passiveだけが抑制」として比較できる。
Eは別のverified official single-target unpair検証が必要。
元pcapが提供/所在確認できれば新規物理試験の前に既存traceを再解析する。

## 検証と成果の境界

今回実行したWindows Release build:

```powershell
& '<VS CMake>/cmake.exe' --build build/stable-identity-hil-resume --config Release --target vut_identity_capture vut_identity_capture_tests vut_backend_pairing_tests vut_setup_pairing_tests vut_protocol_tests
& '<VS CMake>/ctest.exe' --test-dir build/stable-identity-hil-resume -C Release -R '^(protocol|backend_pairing|setup_pairing|identity_capture|identity_compare|identity_offline)$' --output-on-failure --output-junit forensic-results.xml
```

environment: VS18 2026 Insiders / x64 / Python3.13。
cacheのsource directoryは指定research checkout、build directoryはprimary checkout配下の
既存 `build/stable-identity-hil-resume`。source/targetの取り違えを避けて確認した。

- build **PASS**。identity capture executableはbuildのみ、起動していない。
- applicable CTest **6/6 PASS**。fake transportは実機コマンド送信ではない。
- new read-only RF parserのsynthetic check **PASS**:
  opaque word位置、optional report ID、truncation rejection、同一stateのdedup。
- 保存7 capturesの再解析: **INSUFFICIENT_EVIDENCE / failures 0 / bursts 5**。
  これはhardware PASSではない。NADS/NASS各B_HIL_REQUIREDのまま。
- private-value scan **PASS**: 14個の実際のserial/RF表現、漏洩0。JSON/local linksとdiff checkも検証。
- Git/refとstatic binary index生成済み。新規binary取得はofficial SDKのTEMPのみ。
- firmware、HID sharing、Hubなしreconnect、多数tracker、explicit unpairの今回実機確認:
  **NOT RUN**。source/HIL-HIST/API/SAVED-RAWを混同しない。

## 最終分類

```text
Most likely acquisition blocker:
  Mixed: missing reconnect/association completion for latest B and HIL-03;
  missing connect-associated metadata replay for HIL-02 remains possible.
  Passive PWS suppression is a supported HIL-03 contributor candidate.

Connection establishment:
  Partial (saved connect/burst evidence; official CONNECTED mapping unverified)

Safe reconnect sequence:
  Candidate (observed-disconnect + slot-local PWS history only; current B unknown)

Safe device-info refresh:
  Unknown (h228 NA sender found; safety/effect trace insufficient)

Explicit unpair:
  Candidate (official one-id API verified; completion/wire/re-pair proof absent)

NA:
  Unknown
  Recommendation: DO NOT SEND

NADS stability:
  Class B / HIL_REQUIRED; A observed RF+slot changes, B incomplete
NADS acquisition:
  Unresolved; five saved connect bursts, latest B no fresh data

NASS stability:
  Class B / HIL_REQUIRED; separate namespace, same limited scenarios
NASS acquisition:
  Unresolved; five saved connect bursts, latest B no fresh data

Current PairingController correctness:
  Unknown end-to-end; bounded pairing/cleanup software tests acceptable

Next action:
  Passive official-runtime USBPcap trace with fresh B physical confirmation,
  separate HID ownership, first-pose timing, and host-command/response ordering.
  Re-review Gates before any research-only active HIL.
```

成果はresearch branchへcommit/pushのみ。integrationへのmerge、
productionへのcherry-pickは行わない。
