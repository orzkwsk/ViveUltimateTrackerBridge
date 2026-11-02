// View/model structure adapted from codex/ui-ux-hardening 2cfa10f321b62fad4fc99c690f05d339ef91bab4.
// The old GUI-owned process launcher/discovery/stop/config writer is not imported.
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.IO.Pipes;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Windows.Input;
using Microsoft.Win32.SafeHandles;
namespace MonakaVR.Vive.Control;
public sealed record DeviceStatus(string DisplayName, string ObservedId, string? StableId,
    string Connection, string Tracking, string Modality, bool? PositionValid, bool? OrientationValid, double? AgeMs, double? Battery)
{
    public string StableIdentity => StableId ?? "Unknown identity";
    public string Position => PositionValid is null ? "Unknown / stale" : PositionValid.Value ? "Valid" : "Invalid";
    public string Orientation => OrientationValid is null ? "Unknown / stale" : OrientationValid.Value ? "Valid" : "Invalid";
    public string LastUpdate => AgeMs is null ? "Not observed" : $"{AgeMs:F0} ms";
    public string BatteryText => Battery is null ? "Unavailable" : $"{Battery:P0}";
}
public sealed record Status {
    public int Version { get; init; }
    public string? Id { get; init; }
    public bool Ok { get; init; }
    public string Code { get; init; } = "Unknown";
    public string Message { get; init; } = "";
    public string Backend { get; init; } = "Agent unavailable";
    public string Pairing { get; init; } = "Unknown";
    public bool PairingActive { get; init; }
    public bool PairingAvailable { get; init; }
    public bool CleanupConfirmed { get; init; }
    public string CleanupError { get; init; } = "";
    public bool Identifying { get; init; }
    public string[] IdentifyCandidates { get; init; } = [];
    public string LastError { get; init; } = "";
    public string Output { get; init; } = "Unknown";
    public string PhysicalGate { get; init; } = "HIL_PENDING";
    public DeviceStatus[] Devices { get; init; } = [];
}
public interface IControlClient : IAsyncDisposable {
    Task<Status> RequestAsync(string operation, object? arguments, CancellationToken token);
}
public sealed class AgentPipeClient : IControlClient {
    static readonly JsonSerializerOptions Json = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase };
    readonly string name;
    readonly int agentPid;
    readonly Process agent;
    readonly SemaphoreSlim gate = new(1, 1);
    readonly CancellationTokenSource lifetime = new();
    Task? disposing;
    NamedPipeClientStream? pipe;
    bool disposed;
    [DllImport("kernel32.dll", SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    static extern bool GetNamedPipeServerProcessId(SafePipeHandle pipe, out uint pid);
    public AgentPipeClient(string pipeName, int ownerPid) {
        if (!OperatingSystem.IsWindows()) throw new PlatformNotSupportedException();
        const string prefix = "Monaka.Altra.Agent.v1.";
        if (!pipeName.StartsWith(prefix, StringComparison.Ordinal) || !Guid.TryParse(pipeName[prefix.Length..], out _) || ownerPid <= 0)
            throw new ArgumentException("Start this GUI from the Native Agent tray.");
        name = pipeName; agentPid = ownerPid; agent = Process.GetProcessById(ownerPid);
        _ = agent.Handle;
    }
    public bool AgentAlive { get { try { return !agent.HasExited; } catch (InvalidOperationException) { return false; } } }
    void Retire() { pipe?.Dispose(); pipe = null; }
    public async Task<Status> InitializeAsync(CancellationToken token) {
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(token);
        deadline.CancelAfter(TimeSpan.FromSeconds(3));
        while (true) {
            try { return await RequestAsync("get_status", null, deadline.Token).ConfigureAwait(false); }
            catch (IOException) when (AgentAlive && !deadline.IsCancellationRequested) {
                // Startup admission can follow child resume. Only this read-only
                // handshake retries; Pair/Cancel/Identify are never replayed.
                await Task.Delay(100, deadline.Token).ConfigureAwait(false);
            }
        }
    }
    public async Task<Status> RequestAsync(string operation, object? arguments, CancellationToken token) {
        ObjectDisposedException.ThrowIf(disposed, this);
        if (!await gate.WaitAsync(0, token).ConfigureAwait(false)) throw new InvalidOperationException("A request is already running.");
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(token, lifetime.Token);
        timeout.CancelAfter(TimeSpan.FromSeconds(5));
        try {
            if (!AgentAlive) throw new IOException("Agent exited.");
            if (pipe is null) {
                pipe = new NamedPipeClientStream(".", name, PipeDirection.InOut, PipeOptions.Asynchronous);
                await pipe.ConnectAsync(1000, timeout.Token).ConfigureAwait(false);
                if (!GetNamedPipeServerProcessId(pipe.SafePipeHandle, out var server) || server != agentPid)
                    throw new UnauthorizedAccessException("Agent endpoint owner mismatch.");
            }
            string id = Guid.NewGuid().ToString("N");
            var request = new Dictionary<string, object?> { ["version"] = 1, ["id"] = id, ["operation"] = operation };
            if (arguments is not null) foreach (var field in JsonSerializer.SerializeToElement(arguments, Json).EnumerateObject()) request.Add(field.Name, field.Value);
            var bytes = Encoding.UTF8.GetBytes(JsonSerializer.Serialize(request, Json) + "\n");
            if (bytes.Length > 4096) throw new IOException("Request too large.");
            await pipe.WriteAsync(bytes, timeout.Token).ConfigureAwait(false);
            await pipe.FlushAsync(timeout.Token).ConfigureAwait(false);
            using var response = new MemoryStream();
            var one = new byte[1];
            for (;;) {
                if (await pipe.ReadAsync(one, timeout.Token).ConfigureAwait(false) == 0) throw new IOException("Agent disconnected.");
                if (one[0] == 10) break;
                if (response.Length == 16384) throw new IOException("Response exceeds limit.");
                response.WriteByte(one[0]);
            }
            var result = JsonSerializer.Deserialize<Status>(response.ToArray(), Json) ?? throw new IOException("Invalid response.");
            if (result.Version != 1 || result.Id != id || result.Devices is null || result.Devices.Length > 5 || result.IdentifyCandidates is null || result.IdentifyCandidates.Length > 5 ||
                result.Devices.Any(d => d.AgeMs is double age && (!double.IsFinite(age) || age < 0))) throw new IOException("Invalid response/version/correlation.");
            return result;
        } catch (OperationCanceledException) {
            Retire(); throw new IOException("Request cancelled or timed out. Result UNKNOWN; refresh before retrying.");
        } catch { Retire(); throw; }
        finally { gate.Release(); }
    }
    public ValueTask DisposeAsync() => new(disposing ??= DisposeCoreAsync());
    async Task DisposeCoreAsync() {
        disposed = true; lifetime.Cancel();
        await gate.WaitAsync().ConfigureAwait(false);
        try { Retire(); agent.Dispose(); }
        finally { gate.Release(); lifetime.Dispose(); }
    }
}
public sealed class AsyncCommand(Func<Task> execute, Func<bool> enabled) : ICommand {
    public event EventHandler? CanExecuteChanged;
    public bool CanExecute(object? parameter) => enabled();
    public async void Execute(object? parameter) { if (enabled()) await execute(); }
    public void Refresh() => CanExecuteChanged?.Invoke(this, EventArgs.Empty);
}
public sealed class ControlViewModel : INotifyPropertyChanged, IAsyncDisposable {
    readonly IControlClient client;
    readonly CancellationTokenSource lifetime = new();
    readonly List<AsyncCommand> commands = [];
    Task? polling;
    Task? disposing;
    bool busy, closing;
    public bool Busy { get => busy; private set { busy = value; Changed(); UpdateCommands(); } }
    public Status Status { get; private set; } = new();
    public string Message { get; private set; } = "Connecting to the Native Agent...";
    public int Selector { get; set; } = -1;
    public ObservableCollection<DeviceStatus> Devices { get; } = [];
    public AsyncCommand Pair { get; }
    public AsyncCommand Cancel { get; }
    public AsyncCommand Identify { get; }
    public AsyncCommand Refresh { get; }
    bool Connected => Status.Backend == "Agent connected";
    public string Health => $"{Status.Backend} | {Devices.Count} device(s) | {Status.Output}";
    public string IdentifyResult => Status.IdentifyCandidates.Length > 0 ? "Motion candidates (not stable identity): " + string.Join(", ", Status.IdentifyCandidates) : Status.Identifying ? "Observing motion (provisional thresholds)" : "No confirmed identity";
    public event PropertyChangedEventHandler? PropertyChanged;
    public ControlViewModel(IControlClient client) {
        this.client = client;
        Pair = Command("begin_pairing", () => new { selector = Selector, timeoutSeconds = 60 }, () => Connected && Status.PairingAvailable && !Status.PairingActive && Status.CleanupConfirmed);
        Cancel = Command("cancel_pairing", () => null, () => Connected && (Status.PairingActive || !Status.CleanupConfirmed));
        Identify = Command("identify", () => null, () => Connected && !Status.PairingActive);
        Refresh = Command("refresh", () => null, () => true);
    }
    AsyncCommand Command(string op, Func<object?> args, Func<bool> enabled) {
        var command = new AsyncCommand(() => RunAsync(op, args()), () => !Busy && !closing && enabled()); commands.Add(command); return command;
    }
    public async Task RunAsync(string op = "get_status", object? arguments = null) {
        if (Busy || closing) return;
        Busy = true;
        try {
            var value = await client.RequestAsync(op, arguments, lifetime.Token);
            Status = value; Devices.Clear(); foreach (var device in value.Devices) Devices.Add(device);
            Message = value.Message;
            if (!value.PairingAvailable && value.Backend == "Agent connected" && string.IsNullOrEmpty(Message)) Message = "Pairing during tracking: HIL_PENDING. Passive status and Identify are available.";
        } catch (Exception error) {
            Status = new Status(); Devices.Clear(); Message = error.Message;
        } finally {
            Changed(nameof(Status)); Changed(nameof(Health)); Changed(nameof(Message)); Changed(nameof(IdentifyResult)); Busy = false;
        }
    }
    public void StartPolling() { polling ??= PollAsync(); }
    async Task PollAsync() {
        using var timer = new PeriodicTimer(TimeSpan.FromSeconds(1));
        try { do { if (!Busy) await RunAsync(); } while (await timer.WaitForNextTickAsync(lifetime.Token)); }
        catch (OperationCanceledException) { }
    }
    public string Diagnostics() => $"Ultimate Tracker Receiver\n{Health}\nPairing: {Status.Pairing}\nCleanup: {Status.CleanupError}\n{Status.PhysicalGate}\n{Message}";
    void Changed([CallerMemberName] string? name = null) => PropertyChanged?.Invoke(this, new(name));
    void UpdateCommands() { foreach (var command in commands) command.Refresh(); }
    public ValueTask DisposeAsync() => new(disposing ??= DisposeCoreAsync());
    async Task DisposeCoreAsync() {
        closing = true; UpdateCommands(); lifetime.Cancel();
        if (polling is not null) await polling;
        while (Busy) await Task.Delay(10);
        await client.DisposeAsync(); lifetime.Dispose();
    }
}
