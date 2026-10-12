using MonakaVR.Vive.Control;
static void Check(bool value,string reason) { if(!value) throw new Exception(reason); }
var fake=new Fake(); await using(var model=new ControlViewModel(fake)) {
    Check(!model.Pair.CanExecute(null),"pair disabled without owner");
    model.Connect.Execute(null); await fake.Wait(); Check(model.Pair.CanExecute(null),"pair enabled after connection");
    fake.Pending=new(TaskCreationOptions.RunContinuationsAsynchronously);
    model.Pair.Execute(null); model.Pair.Execute(null); Check(model.Busy,"busy during command");
    Check(fake.BeginCount==1,"duplicate operation rejected");
    fake.Pending.SetResult(new Status {Version=1,Ok=true,Backend="Setup owner connected",Pairing="Pairing",PairingActive=true,CleanupConfirmed=false});
    await fake.Wait(); Check(!model.Pair.CanExecute(null) && model.Cancel.CanExecute(null),"pair/cancel state");
    model.Cancel.Execute(null); await fake.Wait(); Check(!model.Status.PairingActive,"cancel completes");
    fake.Next=new Status {Version=1,Ok=true,Backend="Setup owner connected",Pairing="TimedOut",CleanupConfirmed=true,Message="pairing timed out"};
    model.Refresh.Execute(null); await fake.Wait(); Check(model.Message.Contains("timed out"),"timeout actionable");
    fake.Next=new Status {Version=1,Ok=true,Backend="Setup owner connected",CleanupConfirmed=true,Devices=Enumerable.Range(0,5).Select(i=>new DeviceStatus("Tracker",$"rf-{i}",null,"Observed connected",i==1 ? "Lost" : "Tracking",i==1 ? false : true,true,i==2 ? 700 : 10,null)).ToArray()};
    model.Refresh.Execute(null); await fake.Wait(); Check(model.Devices.Count==5 && model.Devices.All(d=>d.BatteryText=="Unavailable"),"five devices and unknown battery");
    fake.Error=new IOException("Service disconnected. Reconnect."); model.Refresh.Execute(null); await fake.Wait(); Check(model.Message.Contains("Reconnect"),"disconnect displayed");
    fake.Error=new IOException("Invalid control response/version"); model.Refresh.Execute(null); await fake.Wait(); Check(model.Message.Contains("Invalid"),"invalid response displayed");
    fake.Next=new Status {Version=1,Ok=true,Backend="Unknown future state",Pairing="Future state"}; model.Refresh.Execute(null); await fake.Wait(); Check(!model.Pair.CanExecute(null),"unknown state fails closed");
    fake.Next=new Status {Version=1,Ok=true,Backend="Setup owner connected",CleanupConfirmed=true};model.Refresh.Execute(null);await fake.Wait();
    model.SelectedObservedId="rf-0";model.MapId="room-map";fake.Error=new IOException("Persistence failed; original preserved");model.SaveMap.Execute(null);await fake.Wait();Check(model.Message.Contains("Persistence"),"persistence failure display");
}
Check(fake.Disposed,"client disposed after shutdown");
var shutdown=new Fake(); var closing=new ControlViewModel(shutdown);
closing.Connect.Execute(null); await shutdown.Wait(); shutdown.Pending=new(TaskCreationOptions.RunContinuationsAsynchronously);
closing.Pair.Execute(null); await closing.DisposeAsync(); Check(shutdown.Disposed && !closing.Busy,"shutdown cancels and awaits operation");
Console.WriteLine("PASS state transitions, commands, duplicate rejection, cancel/timeout, disconnect/invalid/unknown, five devices, shutdown");
sealed class Fake : IControlClient {
    public string Mode {get;private set;}="None"; public string LogDirectory=>"logs";
    public int BeginCount; public bool Disposed;
    public TaskCompletionSource<Status>? Pending; public Exception? Error; public Status? Next;
    public Task StartSetupAsync(CancellationToken token) {Mode="Setup";return Task.CompletedTask;}
    public Task StartServiceAsync(CancellationToken token) {Mode="Service";return Task.CompletedTask;}
    public async Task<Status> RequestAsync(string operation,object? arguments,CancellationToken token) {
        if(operation=="begin_pairing") BeginCount++;
        if(Error is {} e) {Error=null;throw e;}
        if(Pending is {} pending) {try{return await pending.Task.WaitAsync(token);}finally{Pending=null;}}
        if(Next is {} next) {Next=null;return next;}
        return new Status {Version=1,Ok=true,Backend="Setup owner connected",Pairing="Idle",CleanupConfirmed=true};
    }
    public async Task Wait() {await Task.Delay(40);}
    public ValueTask DisposeAsync() {Disposed=true;return ValueTask.CompletedTask;}
}
