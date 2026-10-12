using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Text.Json;
using System.Windows.Input;

namespace MonakaVR.Vive.Control;

public sealed record DeviceStatus(string DisplayName, string ObservedId, string? StableId,
    string Connection, string Tracking, bool? PositionValid, bool? OrientationValid, double? AgeMs, double? Battery)
{
    public string StableIdentity => StableId ?? "Unavailable";
    public string Position => PositionValid is null ? "Unknown" : PositionValid.Value ? "Valid" : "Invalid";
    public string Orientation => OrientationValid is null ? "Unknown" : OrientationValid.Value ? "Valid" : "Invalid";
    public string LastUpdate => AgeMs is null ? "Not available" : $"{AgeMs:F0} ms ago";
    public string BatteryText => Battery is null ? "Unavailable" : $"{Battery:P0}";
}
public sealed record Status
{
    public int Version { get; init; }
    public string? Id { get; init; }
    public bool Ok { get; init; }
    public string Code { get; init; } = "Unknown";
    public string Message { get; init; } = "";
    public string Backend { get; init; } = "Unavailable";
    public string Pairing { get; init; } = "Unknown";
    public bool PairingActive { get; init; }
    public bool CleanupConfirmed { get; init; }
    public string CleanupError { get; init; } = "";
    public string LastError { get; init; } = "";
    public string Output { get; init; } = "Unknown";
    public DeviceStatus[] Devices { get; init; } = [];
}
public interface IControlClient : IAsyncDisposable
{
    string Mode { get; }
    Task<Status> RequestAsync(string operation, object? arguments, CancellationToken token);
    Task StartSetupAsync(CancellationToken token);
    Task StartServiceAsync(CancellationToken token);
    string LogDirectory { get; }
}
public sealed class ProcessControlClient(string root, string buildConfiguration = "Release") : IControlClient
{
    static readonly JsonSerializerOptions Json = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase };
    readonly SemaphoreSlim gate = new(1, 1);
    Process? process;
    Task? logPump;
    bool disposed;
    int? externalPid;
    public string Mode { get; private set; } = "None";
    public string LogDirectory { get; } = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "MonakaVR", "ViveUltimateTrackerBridge", "logs");
    async Task StartAsync(string name, string mode, IEnumerable<string> arguments, CancellationToken token)
    {
        if (process is { HasExited: false }) throw new InvalidOperationException("Stop or disconnect the current owner before switching modes.");
        token.ThrowIfCancellationRequested();
        string exe = Path.Combine(root, "build", "runtime", buildConfiguration, name + ".exe");
        if (!File.Exists(exe)) throw new FileNotFoundException("Control executable is missing. Run Start_GUI.cmd to build it.");
        var start = new ProcessStartInfo(exe) { WorkingDirectory = root, UseShellExecute = false, CreateNoWindow = true, RedirectStandardInput = true, RedirectStandardOutput = true, RedirectStandardError = true };
        foreach (var argument in arguments) start.ArgumentList.Add(argument);
        Directory.CreateDirectory(LogDirectory);
        var logPath = Path.Combine(LogDirectory, $"{name}-{DateTime.UtcNow:yyyyMMdd-HHmmss}-{Guid.NewGuid():N}.log");
        process?.Dispose(); process = Process.Start(start) ?? throw new IOException("The control process could not start.");
        Mode = mode;
        var launched = process;
        logPump = Task.Run(async () =>
        {
            try
            {
                await using var log = new StreamWriter(logPath);
                while (await launched.StandardError.ReadLineAsync() is { } line) { await log.WriteLineAsync(line); await log.FlushAsync(); }
            }
            catch (IOException) { }
            catch (ObjectDisposedException) { }
        });
        await Task.CompletedTask;
    }
    public Task StartSetupAsync(CancellationToken token) => StartAsync("vut_control_host", "Setup", ["--config", Path.Combine(root, "config", "vut-service.conf")], token);
    public Task StartServiceAsync(CancellationToken token)
    {
        var config = Path.Combine(root, "config", "vut-service.conf");
        if (!File.Exists(config)) throw new InvalidOperationException("Tracking configuration is missing. Stable identity and automatic map assignment are unavailable; connect setup and save an explicit observed RF/map configuration on the Advanced tab.");
        var journalRoot = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "MonakaVR", "ViveUltimateTrackerBridge");
        Directory.CreateDirectory(journalRoot);
        return StartAsync("vut_service", "Service", [config, Path.Combine(journalRoot, "vut-service.revision.log"), "--frontend-control"], token);
    }
    public async Task<Status> RequestAsync(string operation, object? arguments, CancellationToken token)
    {
        ObjectDisposedException.ThrowIf(disposed, this);
        if(!OperatingSystem.IsWindows())throw new PlatformNotSupportedException("Native VIVE control requires Windows.");
        if (!await gate.WaitAsync(0, token)) throw new InvalidOperationException("A control request is already running.");
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(token);
        timeout.CancelAfter(TimeSpan.FromSeconds(5));
        try
        {
            if(operation=="stop_service") {
                int? pid=process is {HasExited:false} && Mode is "Service" or "Service unknown" ? process.Id : externalPid;
                if(pid is null)throw new InvalidOperationException("No controllable tracking service is available. Stop an older instance through its launcher.");
                using var target=Process.GetProcessById(pid.Value);
                string expected=Path.GetFullPath(Path.Combine(root,"build","runtime",buildConfiguration,"vut_service.exe"));
                using var self=Process.GetCurrentProcess();
                if(target.SessionId!=self.SessionId||!string.Equals(target.MainModule?.FileName,expected,StringComparison.OrdinalIgnoreCase))throw new InvalidOperationException("Service identity changed. Refresh status before stopping.");
                using var stop=EventWaitHandle.OpenExisting(@"Local\MonakaVR.Vive.ServiceStop.v1."+pid);
                stop.Set();await target.WaitForExitAsync(timeout.Token);Mode="None";externalPid=null;
                return new Status {Version=1,Ok=true,Backend="Tracking service stopped",Pairing="Idle",Message="Tracking service stopped gracefully."};
            }
            if(Mode=="Service unknown" && process is {HasExited:false})return new Status {Version=1,Ok=true,Backend="Tracking service running; status unavailable",Pairing="Unsupported while tracking",Message="Stop and restart tracking to reconnect the frontend status stream."};
            if (process is null || process.HasExited)
            {
                if (process is { HasExited: true }) Mode = "None";
                if (operation != "get_status") throw new InvalidOperationException("The owner is unavailable. Reconnect explicitly and check logs.");
                using var external = new ProcessCollection("vut_service");
                externalPid=null;
                foreach(var candidate in external.Items) {
                    try {
                        string expected=Path.GetFullPath(Path.Combine(root,"build","runtime",buildConfiguration,"vut_service.exe"));
                        if(candidate.SessionId==Process.GetCurrentProcess().SessionId&&string.Equals(candidate.MainModule?.FileName,expected,StringComparison.OrdinalIgnoreCase)) {
                            using var stop=EventWaitHandle.OpenExisting(@"Local\MonakaVR.Vive.ServiceStop.v1."+candidate.Id);
                            if(externalPid is not null){externalPid=null;break;}externalPid=candidate.Id;
                        }
                    }catch(Exception error) when(error is System.ComponentModel.Win32Exception or WaitHandleCannotBeOpenedException or InvalidOperationException){}
                }
                Mode=externalPid is null ? "None" : "External";
                return new Status { Version = 1, Ok = true, Backend = externalPid is not null ? "Tracking service running externally; Stop available" : external.Count > 0 ? "Tracking service running externally; control unavailable" : "Service unavailable", Pairing = "Idle" };
            }
            string id = Guid.NewGuid().ToString("N");
            var request = new Dictionary<string, object?> { ["version"] = 1, ["id"] = id, ["operation"] = operation };
            if (arguments is not null) foreach (var field in JsonSerializer.SerializeToElement(arguments, Json).EnumerateObject()) request.Add(field.Name, field.Value);
            string frame = JsonSerializer.Serialize(request, Json);
            if (frame.Length > 4096) throw new InvalidOperationException("Control request exceeds frame limit.");
            await process.StandardInput.WriteLineAsync(frame.AsMemory(), timeout.Token);
            await process.StandardInput.FlushAsync(timeout.Token);
            var line = new System.Text.StringBuilder();
            var buffer = new char[1];
            while (true)
            {
                if (await process.StandardOutput.ReadAsync(buffer.AsMemory(), timeout.Token) == 0) throw new IOException("Control owner disconnected. Open logs and reconnect.");
                if (buffer[0] == '\n') break;
                if (line.Length == 16384) throw new IOException("Invalid control response size.");
                line.Append(buffer[0]);
            }
            var result = JsonSerializer.Deserialize<Status>(line.ToString(), Json) ?? throw new IOException("Invalid control response.");
            if (result.Version != 1 || result.Id != id || result.Devices.Length > 5 || result.Devices.Any(d => d.AgeMs is < 0 || d.Battery is < 0 or > 1)) throw new IOException("Invalid control response/version. Restart the frontend after checking logs.");
            if (!result.Ok) throw new InvalidOperationException($"{result.Code}: {result.Message}");
            if (operation is "shutdown" or "stop_service")
            {
                await process.WaitForExitAsync(timeout.Token); Mode = "None";
            }
            return result;
        }
        catch (OperationCanceledException)
        {
            // An interrupted request may have reached native ownership. Retire
            // this stream so a late reply cannot be mistaken for another request.
            process?.StandardInput.Close();
            Mode = Mode=="Setup" ? "Setup unknown" : "Service unknown";
            throw new InvalidOperationException("Control request cancelled or timed out. Result is unknown; check diagnostics before retrying. Setup owner performs cleanup on disconnect.");
        }
        finally { gate.Release(); }
    }
    public async ValueTask DisposeAsync()
    {
        if (disposed) return;
        try { if (Mode == "Setup" && process is { HasExited: false }) await RequestAsync("shutdown", null, CancellationToken.None); }
        catch (Exception) { process?.StandardInput.Close(); }
        disposed = true;
        // A normal tracking service continues after GUI close. EOF retires only
        // its frontend worker; it is never killed or implicitly stopped.
        process?.StandardInput.Close();
        if (process is { HasExited: true } && logPump is not null) await logPump;
        process?.Dispose(); gate.Dispose();
    }
    sealed class ProcessCollection : IDisposable
    {
        readonly Process[] processes;
        public int Count => processes.Length;
        public IEnumerable<Process> Items=>processes;
        public ProcessCollection(string name) { processes = Process.GetProcessesByName(name); }
        public void Dispose() { foreach (var value in processes) value.Dispose(); }
    }
}
public sealed class AsyncCommand(Func<Task> execute, Func<bool> enabled) : ICommand
{
    public event EventHandler? CanExecuteChanged;
    public bool CanExecute(object? parameter) => enabled();
    public async void Execute(object? parameter) { if (enabled()) await execute(); }
    public void Refresh() => CanExecuteChanged?.Invoke(this, EventArgs.Empty);
}
public sealed class ControlViewModel : INotifyPropertyChanged, IAsyncDisposable
{
    readonly IControlClient client;
    readonly CancellationTokenSource lifetime = new();
    readonly List<AsyncCommand> commands = [];
    Task? polling;
    CancellationTokenSource? operation;
    bool busy, closing;
    public bool Busy { get => busy; private set { busy = value; Changed(); UpdateCommands(); } }
    public Status Status { get; private set; } = new();
    public string Message { get; private set; } = "Connect setup for first pairing, or start the configured tracking service.";
    public int Selector { get; set; } = -1;
    string? selectedObservedId;
    public string? SelectedObservedId { get => selectedObservedId; set { selectedObservedId = value; Changed(); } }
    public string MapId { get; set; } = "";
    public AsyncCommand SaveMap { get; }
    public ObservableCollection<DeviceStatus> Devices { get; } = [];
    public AsyncCommand Connect { get; }
    public AsyncCommand Pair { get; }
    public AsyncCommand Cancel { get; }
    public AsyncCommand Disconnect { get; }
    public AsyncCommand StartService { get; }
    public AsyncCommand StopService { get; }
    public AsyncCommand Refresh { get; }
    public string Health => $"{Status.Backend} | {Devices.Count} device(s) | Output: {Status.Output}";
    public event PropertyChangedEventHandler? PropertyChanged;
    public ControlViewModel(IControlClient client)
    {
        this.client = client;
        Connect = Command(async () => { if (client.Mode != "Setup") await client.StartSetupAsync(operation!.Token); Apply(await client.RequestAsync("connect", null, operation!.Token)); }, () => client.Mode is "None" or "Setup");
        Pair = Command(async () => Apply(await client.RequestAsync("begin_pairing", new { selector = Selector, timeoutSeconds = 60 }, operation!.Token)), () => client.Mode == "Setup" && Status.Backend == "Setup owner connected" && !Status.PairingActive && Status.CleanupConfirmed);
        Cancel = Command(async () => Apply(await client.RequestAsync("cancel_pairing", null, operation!.Token)), () => client.Mode == "Setup" && (Status.PairingActive || !Status.CleanupConfirmed));
        Disconnect = Command(async () => Apply(await client.RequestAsync("shutdown", null, operation!.Token)), () => client.Mode == "Setup");
        StartService = Command(async () => { await client.StartServiceAsync(operation!.Token); Apply(await client.RequestAsync("get_status", null, operation!.Token)); }, () => client.Mode == "None");
        StopService = Command(async () => Apply(await client.RequestAsync("stop_service", null, operation!.Token)), () => client.Mode is "Service" or "External" or "Service unknown");
        SaveMap = Command(async () =>
        {
            if (string.IsNullOrWhiteSpace(SelectedObservedId) || string.IsNullOrWhiteSpace(MapId)) throw new InvalidOperationException("Select an observed RF address and enter the actual spatial map ID.");
            Apply(await client.RequestAsync("save_map", new { observedId = SelectedObservedId, mapId = MapId }, operation!.Token));
        }, () => client.Mode == "Setup" && !Status.PairingActive);
        Refresh = Command(async () => Apply(await client.RequestAsync("get_status", null, operation!.Token)), () => true);
    }
    AsyncCommand Command(Func<Task> action, Func<bool> enabled) { var command = new AsyncCommand(() => RunAsync(action), () => !Busy && !closing && enabled()); commands.Add(command); return command; }
    public async Task RunAsync(Func<Task> action)
    {
        if (Busy || closing) return;
        Busy = true; operation = CancellationTokenSource.CreateLinkedTokenSource(lifetime.Token);
        try { await action(); }
        catch (Exception e) { Message = e.Message; Changed(nameof(Message)); }
        finally { operation.Dispose(); operation = null; Busy = false; }
    }
    void Apply(Status value)
    {
        Status = value; var selection = SelectedObservedId; Devices.Clear(); foreach (var device in value.Devices) Devices.Add(device); SelectedObservedId = Devices.Any(d => d.ObservedId == selection) ? selection : null;
        Message = value.CleanupConfirmed || client.Mode != "Setup" ? (string.IsNullOrEmpty(value.Message) ? value.LastError : value.Message) : "Cleanup exchange is unconfirmed. "+value.Message+" Check the dongle before retrying setup.";
        Changed(nameof(Status)); Changed(nameof(Health)); Changed(nameof(Message)); UpdateCommands();
    }
    public void StartPolling() { polling ??= PollAsync(); }
    async Task PollAsync()
    {
        using var timer = new PeriodicTimer(TimeSpan.FromSeconds(1));
        try { do { if (!Busy) await RunAsync(async () => Apply(await client.RequestAsync("get_status", null, operation!.Token))); } while (await timer.WaitForNextTickAsync(lifetime.Token)); }
        catch (OperationCanceledException) { }
    }
    public string Diagnostics() => $"Vive Control {typeof(ControlViewModel).Assembly.GetName().Version}\nOS: {Environment.OSVersion.VersionString}\nMode: {client.Mode}\n{Health}\nPairing: {Status.Pairing}\nStable identity: Unavailable\nBody assignment: Unsupported\nBattery: Unavailable\nPrimary result: {Status.Message}\nCleanup error: {Status.CleanupError}\nBackend error: {Status.LastError}\nMessage: {Message.Replace(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "<user>")}\nHIL: NOT RUN";
    public string LogDirectory => client.LogDirectory;
    void Changed([CallerMemberName] string? name = null) => PropertyChanged?.Invoke(this, new(name));
    void UpdateCommands() { foreach (var command in commands) command.Refresh(); }
    public async ValueTask DisposeAsync()
    {
        closing = true; UpdateCommands(); lifetime.Cancel(); operation?.Cancel();
        if (polling is not null) await polling;
        // Wait for a separately running command before disposing its client.
        while (Busy) await Task.Delay(10);
        await client.DisposeAsync(); lifetime.Dispose();
    }
}
