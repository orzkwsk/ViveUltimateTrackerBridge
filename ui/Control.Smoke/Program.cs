using System.IO;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using MonakaVR.Vive.Control;
using MonakaVR.Vive.Ui;
internal static class Program
{
    [STAThread] static int Main()
    {
        var model=new ControlViewModel(new Fake());model.Refresh.Execute(null);
        var window=new ControlWindow(model);
        var root=(FrameworkElement)window.Content;root.Measure(new Size(1050,660));root.Arrange(new Rect(0,0,1050,660));root.UpdateLayout();
        var image=new RenderTargetBitmap(1050,660,96,96,PixelFormats.Pbgra32);image.Render(root);
        var encoder=new PngBitmapEncoder();encoder.Frames.Add(BitmapFrame.Create(image));Directory.CreateDirectory("build/ui-smoke");using(var file=File.Create("build/ui-smoke/vive-five-devices.png"))encoder.Save(file);
        if(model.Devices.Count!=5)throw new InvalidOperationException("Device binding failed.");
        model.DisposeAsync().AsTask().GetAwaiter().GetResult();
        Console.WriteLine("PASS offscreen WPF layout/binding with five devices. Interactive GUI smoke/HIL NOT RUN.");return 0;
    }
    sealed class Fake:IControlClient
    {
        public string Mode=>"None";public string LogDirectory=>"logs";
        public Task StartSetupAsync(CancellationToken token)=>Task.CompletedTask;
        public Task StartServiceAsync(CancellationToken token)=>Task.CompletedTask;
        public Task<Status> RequestAsync(string operation,object? arguments,CancellationToken token)=>Task.FromResult(new Status {Version=1,Ok=true,Backend="Service unavailable (mock)",Pairing="Idle",Output="Unknown",Message="Connect setup, or start the configured tracking service.",Devices=Enumerable.Range(0,5).Select(i=>new DeviceStatus("Observed VIVE tracker",$"01:02:03:04:05:{i:D2}",null,"Observed connected",i==1?"Lost":i==2?"Stale":"Tracking",i==2?(bool?)null:i!=1,i==2?(bool?)null:i!=1,i==2?900:10,null)).ToArray()});
        public ValueTask DisposeAsync()=>ValueTask.CompletedTask;
    }
}
