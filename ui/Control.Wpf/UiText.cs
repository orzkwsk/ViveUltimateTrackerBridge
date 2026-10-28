using System.Globalization;
using System.Windows.Data;

namespace MonakaVR.Vive.Ui;

// Presentation only: protocol values and diagnostics keep their original meaning.
// Unknown backend messages remain visible verbatim, never interpreted as success.
internal sealed class UiText(bool japanese) : IValueConverter {
    static readonly Dictionary<string, string> Japanese = new(StringComparer.Ordinal) {
        ["_Pair"] = "ペアリング(_P)", ["_Cancel pairing"] = "ペアリング中止(_C)",
        ["_Identify"] = "動作で識別(_I)", ["_Refresh"] = "更新(_R)",
        ["_Status"] = "状態(_S)", ["_Advanced"] = "詳細設定(_A)", ["_Diagnostics"] = "診断(_D)",
        ["_Copy diagnostics"] = "診断をコピー(_C)",
        ["Device"] = "デバイス", ["Connection"] = "接続", ["Tracking"] = "追跡", ["Modality"] = "追跡方式",
        ["Position"] = "位置", ["Orientation"] = "姿勢", ["Age"] = "更新から", ["Identity"] = "識別情報",
        ["Observed RF address"] = "観測したRFアドレス", ["Stable identity"] = "永続的な識別情報",
        ["Unknown identity"] = "識別情報不明", ["Unknown / stale"] = "不明／古いデータ",
        ["Valid"] = "有効", ["Invalid"] = "無効", ["Not observed"] = "未観測", ["Unavailable"] = "利用不可",
        ["Unknown"] = "不明", ["Connected"] = "接続済み", ["Disconnected"] = "未接続",
        ["Observed"] = "観測済み", ["Ambiguous"] = "特定不可", ["Tracked"] = "追跡中", ["Lost/degraded"] = "追跡喪失／低下",
        ["Initializing"] = "初期化中", ["Paired event observed"] = "ペアリングイベント観測済み",
        ["STALE"] = "古いデータ", ["UNKNOWN"] = "不明", ["NONE"] = "追跡なし", ["Q-only"] = "姿勢のみ", ["Pos+Q"] = "位置＋姿勢",
        ["HIL_PENDING; stable identity unavailable; Identify thresholds provisional"] = "実機検証待ち／永続的な識別情報なし／動作識別のしきい値は暫定",
        ["Idle"] = "待機中", ["Enabling"] = "開始中", ["Pairing"] = "ペアリング中",
        ["Disabling"] = "終了処理中", ["Completed"] = "完了", ["Cancelled"] = "中止済み",
        ["Cancelling"] = "中止処理中", ["user cancelled"] = "ユーザーが中止しました",
        ["Failed"] = "失敗", ["CleanupFailed"] = "終了処理の確認に失敗",
        ["TimedOut"] = "時間切れ", ["Faulted"] = "エラー", ["HIL_PENDING"] = "実機検証待ち (HIL_PENDING)",
        ["No confirmed identity"] = "識別情報は未確定",
        ["Observing motion (provisional thresholds)"] = "動作を観測中（暫定しきい値）",
        ["Agent unavailable"] = "Agentに接続できません",
        ["Agent connected"] = "Agent接続済み",
        ["Connecting to the Native Agent..."] = "Native Agentに接続中…",
        ["Pairing during tracking: HIL_PENDING. Passive status and Identify are available."] = "追跡中のペアリングは実機検証待ちです。状態確認と動作識別は利用できます。",
        ["Pairing during tracking is HIL_PENDING and disabled in this product configuration."] = "追跡中のペアリングは実機検証待ちのため、この製品構成では無効です。",
        ["Observed, paired event and valid tracking are separate evidence. RF address and slot are not stable identity.\nClosing this window leaves the Agent running. Exit the Native tray to stop tracking and its owned GUI."] =
            "観測・ペアリングイベント・有効な追跡は、それぞれ別の情報です。RFアドレスとスロットは永続的な識別情報ではありません。\nこの画面を閉じてもAgentは動作を続けます。追跡と所有GUIを終了するにはNativeトレイから終了してください。",
        ["Pair selector is a vendor selector, not a tracker number or body part. Pairing during tracking remains disabled until separately qualified. No automatic pairing or reconnect is performed."] =
            "Pair selectorはベンダーの選択値です。トラッカー番号や身体部位ではありません。追跡中のペアリングは別途検証されるまで無効です。自動ペアリングや自動再接続は行いません。",
        ["Native Agent owns config, revision journal and HID. Mapping, World/Mount and output approval belong to MonakaBridge; body assignment and IK belong to MonakaVR."] =
            "Native Agentが設定・リビジョン記録・HIDを所有します。マッピング、World/Mount、出力承認はMonakaBridge、身体割当とIKはMonakaVRの担当です。"
    };
    internal static string Translate(string text, bool japanese) {
        if (!japanese) return text;
        if (Japanese.TryGetValue(text, out var translated)) return translated;
        const string candidates = "Motion candidates (not stable identity): ";
        if (text.StartsWith(candidates, StringComparison.Ordinal)) return "動作候補（永続的な識別情報ではありません）: " + text[candidates.Length..];
        var health = System.Text.RegularExpressions.Regex.Match(text,
            @"^(Agent connected|Agent unavailable) \| (\d+) device\(s\) \| Observation sent: (\d+); downstream visibility unknown$");
        if (health.Success) return $"{Translate(health.Groups[1].Value, true)} | {health.Groups[2].Value}台 | 観測送信: {health.Groups[3].Value}／下流での表示は不明";
        return text;
    }
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture) =>
        Translate(value?.ToString() ?? "", japanese);
    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture) =>
        throw new NotSupportedException();
}
