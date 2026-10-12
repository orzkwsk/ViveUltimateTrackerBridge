using System.IO;
using System.Diagnostics;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using MonakaVR.Vive.Control;

namespace MonakaVR.Vive.Ui;

internal static class Program
{
    [STAThread]
    static int Main(string[] args)
    {
        try
        {
            string root = args.Length > 0 ? Path.GetFullPath(args[0]) : Environment.CurrentDirectory;
            using var single = new Mutex(true, @"Local\MonakaVR.Vive.Control.GUI", out var created);
            if (!created) { MessageBox.Show("VIVE Control is already open."); return 0; }
            var app = new Application();
            app.Run(new ControlWindow(new ControlViewModel(new ProcessControlClient(root, typeof(Program).Assembly.GetCustomAttributes(typeof(System.Reflection.AssemblyConfigurationAttribute), false).OfType<System.Reflection.AssemblyConfigurationAttribute>().FirstOrDefault()?.Configuration ?? "Release")))); return 0;
        }
        catch (Exception e) { MessageBox.Show(e.Message, "VIVE Control startup failed"); return 1; }
    }
}
public sealed class ControlWindow : Window
{
    readonly ControlViewModel model;
    bool canClose;
    public ControlWindow(ControlViewModel model)
    {
        this.model = model; DataContext = model;
        Title = "VIVE Ultimate Tracker Bridge"; Width = 1050; Height = 700; MinWidth = 800; MinHeight = 560;
        var root = new DockPanel { Margin = new Thickness(20), Background = SystemColors.WindowBrush };
        var header = new StackPanel(); DockPanel.SetDock(header, Dock.Top); root.Children.Add(header);
        header.Children.Add(new TextBlock { Text = Title, FontSize = 24, Margin = new Thickness(0, 0, 0, 12) });
        var health = new TextBlock { TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 0, 0, 12) }; health.SetBinding(TextBlock.TextProperty, "Health"); header.Children.Add(health);
        var actions = new WrapPanel(); header.Children.Add(actions);
        Add(actions, "Connect setup", model.Connect); Add(actions, "Pair", model.Pair); Add(actions, "Cancel pairing", model.Cancel);
        Add(actions, "Close setup", model.Disconnect); Add(actions, "Start tracking service", model.StartService); Add(actions, "Stop tracking service", model.StopService); Add(actions, "Refresh", model.Refresh);
        var toolbarCopy=new Button {Content="Copy diagnostics / version",Margin=new Thickness(0,0,8,8)};toolbarCopy.Click+=(_,_)=>Clipboard.SetText(model.Diagnostics());actions.Children.Add(toolbarCopy);
        var toolbarLogs=new Button {Content="Open log folder",Margin=new Thickness(0,0,8,8)};toolbarLogs.Click+=(_,_)=>{Directory.CreateDirectory(model.LogDirectory);Process.Start(new ProcessStartInfo(model.LogDirectory){UseShellExecute=true});};actions.Children.Add(toolbarLogs);
        var tabs = new TabControl { Margin = new Thickness(0, 12, 0, 12) }; root.Children.Add(tabs);
        var normal = new StackPanel(); tabs.Items.Add(new TabItem { Header = "Normal", Content = normal });
        var table = new DataGrid { IsReadOnly = true, AutoGenerateColumns = false, CanUserAddRows = false, MinHeight = 220 };
        foreach (var column in new[] { ("Device", "DisplayName"), ("Connection", "Connection"), ("Tracking", "Tracking"), ("Position", "Position"), ("Orientation", "Orientation"), ("Last update", "LastUpdate"), ("Battery", "BatteryText") }) table.Columns.Add(new DataGridTextColumn { Header = column.Item1, Binding = new Binding(column.Item2) });
        table.SetBinding(ItemsControl.ItemsSourceProperty, "Devices"); normal.Children.Add(table);
        var pairing = new TextBlock { Margin = new Thickness(0, 12, 0, 8) }; pairing.SetBinding(TextBlock.TextProperty, "Status.Pairing"); normal.Children.Add(pairing);
        var message = new TextBlock { TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 8, 0, 8) }; message.SetBinding(TextBlock.TextProperty, "Message"); normal.Children.Add(message);
        normal.Children.Add(new TextBlock { Text = "Pairing needs an explicit click. After Pair, put one tracker in pairing mode.\nStable hardware identity, body assignment and battery: unavailable. Pairing hardware behavior has not been verified.", TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 12, 0, 0) });
        var advanced = new StackPanel { Margin = new Thickness(12) }; tabs.Items.Add(new TabItem { Header = "Advanced", Content = advanced });
        advanced.Children.Add(new TextBlock { Text = "Pair selector is a vendor selector, not a tracker number or body part. Default: all. Effects on a populated dongle remain HIL_REQUIRED.", TextWrapping = TextWrapping.Wrap });
        var selector = new ComboBox { ItemsSource = new[] { -1, 0, 1, 2, 3, 4 }, Width = 160, HorizontalAlignment = HorizontalAlignment.Left, Margin = new Thickness(0, 12, 0, 12) }; selector.SetBinding(System.Windows.Controls.Primitives.Selector.SelectedItemProperty, new Binding("Selector") { Mode = BindingMode.TwoWay }); advanced.Children.Add(selector);
        advanced.Children.Add(new TextBlock { Text = "Tracking requires the existing explicit source/RF/map configuration. RF address is not a persistent device identity.\nReconnect setup closes only its own setup owner; tracking restart is an explicit Stop then Start. External service instances must be stopped in their existing launcher.", TextWrapping = TextWrapping.Wrap });
        advanced.Children.Add(new TextBlock { Text = "Explicit observation admission configuration. Select the observed RF address and name its actual native spatial map. This does not create stable hardware identity or body assignment.", TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 16, 0, 8) });
        var observed = new ComboBox { DisplayMemberPath = "ObservedId", SelectedValuePath = "ObservedId", Width = 280, HorizontalAlignment = HorizontalAlignment.Left }; observed.SetBinding(ItemsControl.ItemsSourceProperty, "Devices"); observed.SetBinding(System.Windows.Controls.Primitives.Selector.SelectedValueProperty, new Binding("SelectedObservedId") { Mode = BindingMode.TwoWay }); advanced.Children.Add(observed);
        var map = new TextBox { Width = 280, HorizontalAlignment = HorizontalAlignment.Left, Margin = new Thickness(0, 8, 0, 8) }; map.SetBinding(TextBox.TextProperty, new Binding("MapId") { Mode = BindingMode.TwoWay, UpdateSourceTrigger = UpdateSourceTrigger.PropertyChanged }); advanced.Children.Add(map);
        Add(advanced, "Save observed RF / map", model.SaveMap);
        var diagnostics = new StackPanel { Margin = new Thickness(12) }; tabs.Items.Add(new TabItem { Header = "Diagnostics / Research", Content = diagnostics });
        var copy = new Button { Content = "Copy diagnostics", Margin = new Thickness(0, 0, 0, 8) }; copy.Click += (_, _) => Clipboard.SetText(model.Diagnostics()); diagnostics.Children.Add(copy);
        var log = new Button { Content = "Open log folder" }; log.Click += (_, _) => { Directory.CreateDirectory(model.LogDirectory); Process.Start(new ProcessStartInfo(model.LogDirectory) { UseShellExecute = true }); }; diagnostics.Children.Add(log);
        var rf = new DataGrid { IsReadOnly = true, AutoGenerateColumns = false, CanUserAddRows = false, Margin = new Thickness(0, 12, 0, 12) };
        rf.Columns.Add(new DataGridTextColumn { Header = "Observed RF address", Binding = new Binding("ObservedId") }); rf.Columns.Add(new DataGridTextColumn { Header = "Stable identity", Binding = new Binding("StableIdentity") }); rf.SetBinding(ItemsControl.ItemsSourceProperty, "Devices"); diagnostics.Children.Add(rf);
        diagnostics.Children.Add(new TextBlock { Text = "No raw HID, unpair or experimental recovery command is exposed. Closing setup cancels pairing with bounded best-effort disable. Closing this window leaves a normal tracking service running.", TextWrapping = TextWrapping.Wrap });
        Content = root;
        Loaded += (_, _) => model.StartPolling();
        Closing += async (_, e) => { if (canClose) return; e.Cancel = true; IsEnabled = false; await model.DisposeAsync(); canClose = true; _ = Dispatcher.BeginInvoke(new Action(Close)); };
    }
    static void Add(Panel panel, string label, AsyncCommand command) => panel.Children.Add(new Button { Content = label, Command = command, Padding = new Thickness(10, 6, 10, 6), Margin = new Thickness(0, 0, 8, 8) });
}
