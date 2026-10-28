using MonakaVR.Vive.Control;
using System.Diagnostics;
using System.IO.Pipes;
using System.Text.Json;
static void Check(bool value, string reason) { if (!value) throw new Exception(reason); }
static async Task Idle(ControlViewModel model) {
    using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(3));
    while (model.Busy) await Task.Delay(1, timeout.Token);
}
var fake = new Fake();
await using (var model = new ControlViewModel(fake)) {
    Check(!model.Pair.CanExecute(null) && !model.Identify.CanExecute(null), "disconnected commands denied");
    await model.RunAsync();
    Check(model.Pair.CanExecute(null) && model.Identify.CanExecute(null), "explicit capability enables commands");
    fake.Pending = new(TaskCreationOptions.RunContinuationsAsynchronously);
    model.Pair.Execute(null); model.Pair.Execute(null);
    Check(model.Busy && fake.BeginCount == 1, "duplicate request rejected");
    fake.Pending.SetResult(Fake.Connected with { Pairing = "Pairing", PairingActive = true, CleanupConfirmed = false });
    await Idle(model);
    Check(!model.Pair.CanExecute(null) && model.Cancel.CanExecute(null) && !model.Identify.CanExecute(null), "active pairing command gates");
    model.Cancel.Execute(null); await Idle(model);
    Check(!model.Status.PairingActive && model.Pair.CanExecute(null), "cancel completes");
    fake.Next = Fake.Connected with { Pairing = "TimedOut", Message = "pairing timed out" };
    await model.RunAsync(); Check(model.Message.Contains("timed out"), "timeout visible");
    fake.Next = Fake.Connected with { Pairing = "CleanupFailed", CleanupConfirmed = false };
    await model.RunAsync(); Check(!model.Pair.CanExecute(null) && model.Cancel.CanExecute(null), "cleanup uncertainty blocks enable");
    fake.Next = Fake.Connected with { PairingAvailable = false };
    await model.RunAsync(); Check(!model.Pair.CanExecute(null) && model.Identify.CanExecute(null), "production RF gate explicit");
    fake.Error = new IOException("Result UNKNOWN; refresh before retrying");
    await model.RunAsync(); Check(!model.Pair.CanExecute(null) && !model.Identify.CanExecute(null) && model.Devices.Count == 0, "transport error retires stale permissions");
    fake.Next = Fake.Connected with { Identifying = true, IdentifyCandidates = ["observed-rf"] };
    await model.RunAsync(); Check(model.IdentifyResult.Contains("observed-rf") && model.IdentifyResult.Contains("not stable"), "Identify evidence distinct");
    fake.Next = Fake.Connected with { Backend = "Future unknown state" };
    await model.RunAsync(); Check(!model.Pair.CanExecute(null), "unknown state fails closed");
}
Check(fake.Disposed, "GUI close disposes connection only");
var pending = new Fake(); var closing = new ControlViewModel(pending);
await closing.RunAsync(); pending.Pending = new(TaskCreationOptions.RunContinuationsAsynchronously);
closing.Pair.Execute(null);
var firstClose=closing.DisposeAsync().AsTask(); var secondClose=closing.DisposeAsync().AsTask();
await Task.WhenAll(firstClose,secondClose);
Check(pending.Disposed && !closing.Busy, "close cancels and awaits outstanding request");
Check(pending.DisposeCount==1,"concurrent close disposes once");
Console.WriteLine("PASS UI commands, duplicate, timeout, cleanup, capability, unknown/disconnect, Identify, concurrent close");
if(OperatingSystem.IsWindows()) {
    var name="Monaka.Altra.Agent.v1."+Guid.NewGuid().ToString("B");
    using var timeout=new CancellationTokenSource(TimeSpan.FromSeconds(8));
    var requests=new List<string>();
    var server=Task.Run(async()=>{
        for(int attempt=0;attempt<2;attempt++) {
            await using var pipe=new NamedPipeServerStream(name,PipeDirection.InOut,1,PipeTransmissionMode.Byte,PipeOptions.Asynchronous);
            await pipe.WaitForConnectionAsync(timeout.Token);
            using var reader=new StreamReader(pipe,leaveOpen:true);
            var raw=await reader.ReadLineAsync(timeout.Token)??throw new Exception("missing handshake");
            var request=JsonDocument.Parse(raw);requests.Add(request.RootElement.GetProperty("operation").GetString()!);
            if(attempt==0)continue;
            using var writer=new StreamWriter(pipe,leaveOpen:true){AutoFlush=true};
            await writer.WriteLineAsync(JsonSerializer.Serialize(new {version=1,id=request.RootElement.GetProperty("id").GetString(),ok=true,backend="Agent connected",devices=Array.Empty<object>(),identifyCandidates=Array.Empty<string>()}));
        }
    });
    await using var client=new AgentPipeClient(name,Environment.ProcessId);
    var initial=await client.InitializeAsync(timeout.Token);
    await server;
    Check(initial.Ok && requests.SequenceEqual(new[]{"get_status","get_status"}),"read-only admission retry");
    Console.WriteLine("PASS exact-owner namedpipe initial disconnect then read-only retry");
}
sealed class Fake : IControlClient {
    public static Status Connected => new() { Version = 1, Ok = true, Backend = "Agent connected", Pairing = "Idle", PairingAvailable = true, CleanupConfirmed = true };
    public int BeginCount, DisposeCount; public bool Disposed;
    public TaskCompletionSource<Status>? Pending;
    public Status? Next; public Exception? Error;
    public async Task<Status> RequestAsync(string operation, object? args, CancellationToken token) {
        if (operation == "begin_pairing") ++BeginCount;
        if (Error is { } error) { Error = null; throw error; }
        if (Pending is { } pending) { try { return await pending.Task.WaitAsync(token); } finally { Pending = null; } }
        if (Next is { } next) { Next = null; return next; }
        return Connected;
    }
    public ValueTask DisposeAsync() { Disposed = true; ++DisposeCount; return ValueTask.CompletedTask; }
}
