// View adapted from codex/ui-ux-hardening 2cfa10f321b62fad4fc99c690f05d339ef91bab4.
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Threading;
using MonakaVR.Vive.Control;
namespace MonakaVR.Vive.Ui;
internal static class Program {
    [STAThread]
    static int Main(string[] args) {
        try {
            if (args.Length != 4 || args[0] != "--agent-pipe" || args[2] != "--agent-pid" || !int.TryParse(args[3], out var pid))
                throw new ArgumentException("Open Ultimate Tracker Receiver from its Native Agent tray.");
            var client = new AgentPipeClient(args[1], pid);
            using var single = new Mutex(true, @"Local\Monaka.Altra.GUI." + args[1], out var created);
            if (!created) { client.DisposeAsync().GetAwaiter().GetResult(); return 2; }
            try {
                var initial = client.InitializeAsync(CancellationToken.None).GetAwaiter().GetResult();
                if (!initial.Ok || !client.AgentAlive) throw new InvalidOperationException("Native Agent handshake failed.");
                var app = new Application { ShutdownMode = ShutdownMode.OnMainWindowClose };
                var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(250) };
                timer.Tick += (_, _) => { if (!client.AgentAlive) app.Shutdown(1); };
                timer.Start();
                app.Run(new ControlWindow(new ControlViewModel(client)));
                timer.Stop();
                return 0;
            } finally { client.DisposeAsync().GetAwaiter().GetResult(); }
        } catch (Exception error) {
            Console.Error.WriteLine("Ultimate Tracker Receiver: " + error.Message);
            return 1;
        }
    }
}
public sealed class ControlWindow : Window {
    readonly ControlViewModel model;
    bool canClose;
    public ControlWindow(ControlViewModel model) {
        this.model = model; DataContext = model;
        Title = "Ultimate Tracker Receiver"; Width = 1050; Height = 700; MinWidth = 800; MinHeight = 560;
        var root = new DockPanel { Margin = new Thickness(20), Background = SystemColors.WindowBrush };
        var header = new StackPanel(); DockPanel.SetDock(header, Dock.Top); root.Children.Add(header);
        header.Children.Add(new TextBlock { Text = Title, FontSize = 24, Margin = new Thickness(0,0,0,12) });
        var health = new TextBlock { TextWrapping = TextWrapping.Wrap }; health.SetBinding(TextBlock.TextProperty, "Health"); header.Children.Add(health);
        var actions = new WrapPanel { Margin = new Thickness(0,12,0,0) }; header.Children.Add(actions);
        Add(actions, "_Pair", model.Pair); Add(actions, "_Cancel pairing", model.Cancel); Add(actions, "_Identify", model.Identify); Add(actions, "_Refresh", model.Refresh);
        var tabs = new TabControl { Margin = new Thickness(0,12,0,12) }; root.Children.Add(tabs);
        var normal = new StackPanel(); tabs.Items.Add(new TabItem { Header = "_Status", Content = new ScrollViewer { Content = normal, VerticalScrollBarVisibility = ScrollBarVisibility.Auto } });
        var table = new DataGrid { IsReadOnly = true, AutoGenerateColumns = false, CanUserAddRows = false, MinHeight = 220 };
        foreach (var column in new[] { ("Device","DisplayName"), ("Connection","Connection"), ("Tracking","Tracking"), ("Modality","Modality"), ("Position","Position"), ("Orientation","Orientation"), ("Age","LastUpdate"), ("Identity","StableIdentity") })
            table.Columns.Add(new DataGridTextColumn { Header = column.Item1, Binding = new Binding(column.Item2) });
        table.SetBinding(ItemsControl.ItemsSourceProperty, "Devices"); normal.Children.Add(table);
        foreach (var binding in new[] { "Status.Pairing", "IdentifyResult", "Message", "Status.CleanupError", "Status.PhysicalGate" }) {
            var label = new TextBlock { TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0,8,0,0) };
            label.SetBinding(TextBlock.TextProperty, binding); normal.Children.Add(label);
        }
        normal.Children.Add(new TextBlock { Text = "Observed, paired event and valid tracking are separate evidence. RF address and slot are not stable identity.\nClosing this window leaves the Agent running. Exit the Native tray to stop tracking and its owned GUI.", TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0,12,0,0) });
        var advanced = new StackPanel { Margin = new Thickness(12) }; tabs.Items.Add(new TabItem { Header = "_Advanced", Content = advanced });
        advanced.Children.Add(new TextBlock { Text = "Pair selector is a vendor selector, not a tracker number or body part. Pairing during tracking remains disabled until separately qualified. No automatic pairing or reconnect is performed.", TextWrapping = TextWrapping.Wrap });
        var selector = new ComboBox { ItemsSource = new[] { -1,0,1,2,3,4 }, Width = 160, HorizontalAlignment = HorizontalAlignment.Left, Margin = new Thickness(0,12,0,12) };
        selector.SetBinding(System.Windows.Controls.Primitives.Selector.SelectedItemProperty, new Binding("Selector") { Mode = BindingMode.TwoWay }); advanced.Children.Add(selector);
        advanced.Children.Add(new TextBlock { Text = "Native Agent owns config, revision journal and HID. Mapping, World/Mount and output approval belong to MonakaBridge; body assignment and IK belong to MonakaVR.", TextWrapping = TextWrapping.Wrap });
        var diagnostics = new StackPanel { Margin = new Thickness(12) }; tabs.Items.Add(new TabItem { Header = "_Diagnostics", Content = diagnostics });
        var copy = new Button { Content = "_Copy diagnostics", HorizontalAlignment = HorizontalAlignment.Left, Padding = new Thickness(10,6,10,6) };
        copy.Click += (_, _) => { try { Clipboard.SetText(model.Diagnostics()); } catch (System.Runtime.InteropServices.ExternalException) { } }; diagnostics.Children.Add(copy);
        var rf = new DataGrid { IsReadOnly = true, AutoGenerateColumns = false, CanUserAddRows = false, Margin = new Thickness(0,12,0,12) };
        rf.Columns.Add(new DataGridTextColumn { Header = "Observed RF address", Binding = new Binding("ObservedId") });
        rf.Columns.Add(new DataGridTextColumn { Header = "Stable identity", Binding = new Binding("StableIdentity") });
        rf.SetBinding(ItemsControl.ItemsSourceProperty, "Devices"); diagnostics.Children.Add(rf);
        Content = root;
        Loaded += (_, _) => model.StartPolling();
        Closing += async (_, e) => { if (canClose) return; e.Cancel = true; IsEnabled = false; await model.DisposeAsync(); canClose = true; _ = Dispatcher.BeginInvoke(new Action(Close)); };
    }
    static void Add(Panel panel, string label, AsyncCommand command) => panel.Children.Add(new Button { Content = label, Command = command, Padding = new Thickness(10,6,10,6), Margin = new Thickness(0,0,8,8) });
}
