# Codex Task 3: VIVE Backend

単独投入用実装指示書。Task 3 は `orzkwsk/ViveUltimateTrackerBridge` だけを変更する。

**契約状態：C1候補版 / master reconciliation pending。** 共通wire契約はTask 1 `MonakaProtocol`が唯一の所有者であり、このrepoで変更・再定義しない。まずroot `AGENTS.md` と `docs/codex/task3/00_task1_handoff_status.md` を全文読むこと。

## Repository / audit

| 項目 | 値 |
|---|---|
| 対象repo | `orzkwsk/ViveUltimateTrackerBridge` |
| default branch | `main` |
| audited base branch | `dev/vivehub-bypass` |
| audited base SHA | `db7c6795055fc00054b43a64e3bbd658a3575932` |
| prepared work branch | `refactor/monaka-layer-separation` |
| C1 SHA256 | `3a76435c8b25b3975028f9a83c4dc3d1e3848806c9608d885f5bba74a1198ed3` |

開始時に `git fetch --no-prune origin`、worktree、現在HEAD、`AGENTS.md`を確認する。Codex Cloudでlocal branchが`work`と表示される場合はAGENTS.mdの例外規則を適用し、branch名だけを理由に停止しない。重要なのはprepared branch/baseからのancestryである。既存branch削除、force push、rebase、amend、resetによる履歴差し替えは禁止。mainへ直接実装しない。

## Task 3の責務

VIVE Ultimate Tracker → official dongle → Windows HID/SetupAPI → vendor protocol decode → backend observation → C1 `TrackerObservation` / `ObservationDeviceState` publicationを担当する。

起点実装の以下を維持する。

- `include/vut/backend.hpp`: `Backend::{start,stop,pollOnce,setEventCallback,snapshot}`
- `src/backend.cpp`: Windows HID/SetupAPI、official dongle制御
- `include/vut/protocol.hpp`, `src/protocol.cpp`: `parsePairEvent`, `parseIncomingEnvelope`, `parsePose`, feature command/ACK生成
- `include/vut/types.hpp`: `DeviceId`, `TrackerState`, `TrackingStatus`, `BackendEvent` はvendor内部型として維持
- `tools/vut_probe.cpp`: 診断CLIとして維持
- CMake既存target: `vut_backend`, `vut_probe`, `vut_protocol_tests`
- repoはC++20のまま。Task 1 codecがC++17であっても既存decoderの要件を下げない。

追加する主要境界は `ViveObservationAdapter`、`ObservationPublisher`、Backend service entry point。probeからのC1送信はoptionalでよい。

## 非責務

このTaskで以下を実装しない。

- VIVE Hub起動/自動化
- Wi-Fi-only接続
- 接続個数制限の回避
- SteamVR server driver
- world/body calibration
- body role/IK target
- Fusion/Fallback/IK
- ベンダー未確定値からのbattery/derivative推定
- 新規の第三者reverse-engineering sourceコピー

今回の目標は**Hubを通さない既存official-dongle/HID backendをMonaka共通観測境界へ接続できる形に整理すること**であり、将来のWi-Fi/個数制限回避は別phaseとする。

## Task 1 protocol dependency

`docs/codex/task3/00_task1_handoff_status.md`記載の固定artifactだけを使用する。

正規値:

- kit: `monaka-protocol-kit-v1.0.zip`
- kit SHA256: `eef5b7f2bc490926385b99dabcd44dc5a374228bf2a7869beea01f9dad936729`
- source commit: `5f41586b51bd84bb1cea344879b5d6325fc2c47d`
- schema commit: `04f2d6c831c68a65edfbfb66ff8838d1c9d78535`
- C1 SHA256: `3a76435c8b25b3975028f9a83c4dc3d1e3848806c9608d885f5bba74a1198ed3`
- protocol lock SHA256: `234f0dffc46b808a179ec91da3c185794d7b7c83bc5b7d2bcb31fa73886bcb44`

kitを受け取ったらhash/manifest/internal sumsを検証し、`third_party/monaka-protocol/`へ固定取り込み、依存provenanceを`dependencies/`に残す。`MonakaProtocol::Codec`を利用し、独自JSON codecや同名の標準pose modelを作らない。

このTask文書はvendor→C1 mapping policyを規定するが、C1 wire fieldの定義そのものはimportしたkitの`docs/C1.md`、schema、public headersが規範である。kitが無い場合はcontract依存実装を完了扱いにしない。

## 実装要件

### 1. Vendor state → Observation pure adapter

`TrackerState` / backend eventからC1 modelへ変換するpure/testableなadapterを追加する。vendor内部型はvendor境界内に残し、generic consumerへVIVE packet/typeを漏らさない。

MAC identity:

- `DeviceId::toString()`のlowercase、colon区切りfull 6-byte MACをC1 `device_id`に使う。
- slot、列挙順、RF packet indexは永続IDにしない。
- `source_id`はBackend installation/config単位の永続IDとする。

### 2. Pose / state stream separation

pair/unpair eventとposeは別streamとして扱う。

- `DevicePaired` → `ObservationDeviceState` を発行。
- `DeviceUnpaired` → `presence=absent` を発行し、そのdeviceのpose/cacheを無効化する。
- 実在しない汎用pair/unpair APIが完成していると主張しない。現repoの既存event/bring-up機能を正確に包む。
- device_stateはpose freshnessを更新しない。

### 3. Sequence / timestamp / session

- pose admissionごとにC1 pose sequenceを増加させる。
- HID poseを受信した時刻をimmutable receive timestampとして一度だけ確定する。`timestamp_kind=receive`。
- RFの8-bit `packetIndex`をC1 sequenceに使わない。
- packetIndexのwrapをBackend restart/session restartと誤認しない。
- duplicate/reorderの既存挙動を調査し、replayされた古いposeでfreshnessを進めない。
- Backend restart / dongle session lossではC1 sessionを更新し、旧sessionのposeを新sessionでfresh poseとして再発行しない。
- wall clockをsample timestampに使用しない。

### 4. Liveness / loss

- HID transport timeoutとoptical tracking lossを同一視しない。
- unpair eventが欠落した場合でもpose timeoutによりconsumer側でfreshnessを失える情報を出す。
- 数値が静止しているだけでstale扱いしない。
- metadata/state packetだけでpose freshnessを更新しない。

### 5. Publication / backpressure

C1 Observationの既定送信先は`127.0.0.1:29810`。

- send失敗、Bridge未起動、consumer停止でHID polling threadを止めない。
- per-device latest-only stateと有界queue/非blocking送信を用いる。
- malformed/encode/send errorは隔離・計数し、次の正常sample処理を継続する。
- C1 publicationを無効化しても`vut_probe`診断機能は継続できること。
- LAN bindを自動で行わない。

### 6. Coordinate space

- backend native conventionは`vut-native-v1`として宣言する。
- `space.id`は現在のSLAM/map identityを表す設定値とする。
- 複数trackerが同一mapを共有することが未確認なら、名前やaxisが同じという理由だけで同一space.idへ統合しない。
- map reset/restartでorigin/map意味が変わる場合はrevisionを更新し、旧revisionのcacheをfreshとして流用しない。
- world calibration / OpenVR transform / body mount transformはTask 3へ入れない。

## VIVE → C1 mapping

| vendor値 | C1 policy |
|---|---|
| `raw=0x02/0x12 (Ok)` | 数値検証を通ったposition/orientationをvalid。両方validなら`FULL / tracked`。orientation evidence=`device` |
| `raw=0x03/0x13 (Lost, pre-localization)` | position/orientationともCommon Observationではinvalid、`NONE / lost`。vendor quaternionが数値上動いても初回localizationでframe jumpが実測されているため使用しない |
| `raw=0x04/0x14 (RecentlyLost, post-localization)` | position invalid。Quaternionが数値検証を通ればorientation valid、`ROTATION_ONLY / degraded`、orientation evidence=`device` |
| `status=0/1/5` | 両component invalid、`initializing` |
| unknown / unconfirmed raw status | 両component invalid、`unknown` |
| `positionMeters` | metresとして既存実装を維持。ただし実機1m検証未実施なら未確認と明記 |
| `orientationXyzw` | 既存wire `[w,z,y,x]`→internal xyzw変換を維持。Task 3でさらに並べ替えない |
| `linearAccelerationRaw` | C1ではnull。単位/frame/gravity semantics未確定 |
| `angularVelocityRaw[4]` | C1ではnull。4値を都合よく3軸角速度へ切り詰めない |
| battery | null。fractionを推測しない |
| buttons/ACK/RF/raw payload | vendor診断・制御内だけに保持しObservation extensionへ流さない |

数値validationによる部分有効化は、HILで意味が確認されたstatusだけに適用する。現行ではOkは両componentが有効なときだけFULL、RecentlyLost 0x04/0x14はorientationのみ有効ならROTATION_ONLYとする。Lost 0x03/0x13はQuaternionが数値上正常でもNONEのままにする。vendor statusが`initializing`または`unknown`を示す場合も数値だけで昇格しない。

capabilitiesは**現在意味を保証できるものだけ**を列挙する。初期はposition/orientationを基本とし、derivative/battery capabilityを未検証のまま宣言しない。

QuaternionはfiniteかつC1 Observation許容normを満たす場合のみvalid。Backendではvendor sample値を保持し、Task 3で勝手にworld-normalize/calibrateしない。

## 既知のdecoder不具合 — 必須修正

`halfToFloat()`のsubnormal経路は現在指数が1小さい。

既知再現:

```text
input    0x0001
current  2.980232239e-08
expected 5.960464478e-08 (2^-24)
```

必ず以下のfixtureを追加して再現確認してから修正する。

- `0x0001` → 2^-24
- `0x03ff` 最大subnormal
- +0 / -0
- normal境界
- +Inf / -Inf / NaN behavior

この修正は**他のlayer分離作業と分けた小さいcommit**にする。公開reverse-engineering codeをコピーして置換しない。

## Tests / build

### Unit

既存protocol testsを維持し、少なくとも以下を追加する。

- raw status 0x02/0x12, 0x03/0x13, 0x04/0x14, unknown mapping
- 37/39-byte pose variants
- truncation / malformed frame
- nonfinite vector/quaternion
- zero quaternion
- MAC identityとslot再利用
- pair/unpair
- RF packetIndex wrap/duplicate/reorder behavior
- `halfToFloat` subnormal/boundaries
- Observation mapping / capability omission
- session restart / retired sample
- stopped C1 consumerでもbackend processingが進むこと

既存testが`assert`依存でRelease/NDEBUG時に無検証になる場合は修正する。test targetでassertを有効化するか、明示的な失敗return/exception/checkへ変える。**意図的に壊したfixtureでtestがFAILすることも一度確認**し、その確認方法を報告する。

### Mock end-to-end

固定Task1 kitがある場合:

`HID receive fixture -> BackendEvent/vendor state -> ViveObservationAdapter -> MonakaProtocol::Codec encode -> common decoder/mock consumer`

を検証する。generic mockはVIVEのpacket/header/typeへ依存してはならない。

加えて、以下をmockで検証する。

- 2 sourceで同じslot/addressが出てもpersistent identityが衝突しない
- event欠落
- receiver/service停止と再接続
- late retired session packet
- metadataのみ継続
- consumer停止/backpressure

### Windows build

基本コマンド:

```text
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

実環境にgenerator差異がある場合は実際のコマンドを報告する。

non-Windowsではparser/adapter/mockのみを検証してよいが、Windows HID/SetupAPI成功とは扱わない。

### Hardware validation

可能なら以下を実機で検証するが、未実施項目はNOT RUNにする。

- VIVE Hub停止状態でofficial dongle open
- 1 tracker pose
- 複数tracker pose
- status loss/recovery
- unpair / power-cycle
- dongle disconnect/reconnect
- map reset/restart
- 1m並進
- 各軸rotation / quaternion convention

Wi-Fi-only / tracker-count-limit bypassはTask 3のhardware test対象外。

## Documentation

`docs/INITIAL_RESEARCH.md`とREADMEを更新し、次を明確に分ける。

- 取得/実装済み
- mockで検証済み
- Windowsで検証済み
- hardwareで検証済み
- 未確定/未実機

Hubなし実機運用、battery、derivative、map identityなどを検証していない場合は成功済みと書かない。

## Compatibility

official dongle、Windows HID/SetupAPI、既存VID/PID filter、pair event、RF ACK bring-up、37/39-byte pose decoderを維持する。

既知未完成:

- map-transfer recovery
- battery normalization
- acceleration semantics
- angular-velocity frame
- 将来のWi-Fi接続
- tracker-count-limit回避

これらをTask 3完了条件へ混入させない。

## Definition of Done

Task 3完了には以下が必要。

1. fixed Task1 protocol kitがhash検証済みで取り込まれている。
2. generic Observation mock consumerがVIVE packet/typeに依存せず動く。
3. existing HID/backend/decoder/probe機能を維持している。
4. VIVE native stateからC1へのmappingがunit testされている。
5. sequence/session/timestamp/identity rulesがshort vendor countersから分離されている。
6. unknown battery/derivative semanticsがnullのまま保持されている。
7. `halfToFloat()` subnormal bugがfixture付きの独立commitで修正されている。
8. SteamVR/OpenVR/solver/calibration dependencyをBackendへ追加していない。
9. build/test/hardwareの実績とNOT RUNを区別している。
10. Task 4へ、protocol provenance/lock、VIVE sample fixtures、capabilities、`vut-native-v1` space profile検証状況、known limitationsをhandoffできる。

kitが無い場合は`blocked on fixed Task 1 artifact`として止め、DoD completeと報告しない。

## 最終報告形式

以下を実値で埋める。実行していないものをPASSにしない。

```text
repository:
audited_base_branch: dev/vivehub-bypass
audited_base_sha: db7c6795055fc00054b43a64e3bbd658a3575932
actual_base_branch:
actual_base_sha:
base_change_reason:
work_branch: refactor/monaka-layer-separation (or Codex-managed work branch with verified ancestry)
HEAD_SHA:
protocol_version:
schema_commit:
protocol_kit_sha256:
contract_c1_sha256:

changed_files: path + reason
build_result: command / environment / PASS|FAIL|NOT RUN
unit_test_result:
mock_or_cross_language_result:
hardware_validation:
compatibility_status:
phase_or_DoD_status:
unresolved_issues:
followup_required_in_other_repos:
handoff_artifacts: filename / SHA256 / source commit
```

最後に「VIVE Backendに残した責務」「上位Bridge/MonakaVRへ残した責務」「旧diagnostic/backend利用を壊していない根拠」を短くまとめる。


## 2026-09-22 HIL update

このTask文書の初期仮説より後に実機status semanticsが追加検証された。現在の規範的なHIL根拠は
`docs/hil/vive-tracking-status-semantics.md` と
`docs/hil/raw/2026-09-22-vive-tracking-status-pose-only.txt`。
次回Codex作業では、旧記述だけを前提にせずこれらを先に確認すること。
必要なHIL情報が不足する場合は推測で補完せず、必要なログ/操作条件をユーザーへ具体的に要求すること。
