# Research only. No HID open or vendor command is implemented here.
Set-StrictMode -Version Latest
$script:HealthModulePath=$PSCommandPath

function Initialize-HealthConsole {
    if ('UsbHealthConsole' -as [type]) { return }
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.ComponentModel;
public static class UsbHealthConsole {
    [StructLayout(LayoutKind.Explicit, Size=20)]
    public struct InputRecord {
        [FieldOffset(0)] public ushort EventType;
        [FieldOffset(4)] public int KeyDown;
        [FieldOffset(8)] public ushort Repeat;
        [FieldOffset(10)] public ushort VirtualKey;
        [FieldOffset(12)] public ushort ScanCode;
        [FieldOffset(14)] public char Character;
        [FieldOffset(16)] public uint ControlState;
    }
    [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr GetStdHandle(int id);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool GetConsoleMode(IntPtr h, out uint mode);
    [DllImport("kernel32.dll", SetLastError=true)] static extern uint GetConsoleProcessList([Out] uint[] ids, uint size);
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    static extern bool WriteConsoleInputW(IntPtr h, InputRecord[] records, uint size, out uint written);
    public static uint[] Members() {
        uint[] ids = new uint[256]; uint n = GetConsoleProcessList(ids, (uint)ids.Length);
        if (n == 0 || n > ids.Length) throw new Win32Exception(Marshal.GetLastWin32Error());
        Array.Resize(ref ids, (int)n); return ids;
    }
    public static bool HasInput() {
        uint mode; return GetConsoleMode(GetStdHandle(-10), out mode);
    }
    public static void SendQ() {
        IntPtr h = GetStdHandle(-10); uint mode;
        if (!GetConsoleMode(h, out mode)) throw new Win32Exception(Marshal.GetLastWin32Error());
        InputRecord down = new InputRecord { EventType=1, KeyDown=1, Repeat=1, VirtualKey=0x51, ScanCode=0x10, Character='q' };
        InputRecord up = down; up.KeyDown=0;
        uint written;
        if (!WriteConsoleInputW(h, new InputRecord[] { down, up }, 2, out written) || written != 2)
            throw new Win32Exception(Marshal.GetLastWin32Error());
    }
}
'@
}

function Get-HealthHeader([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { return 'ABSENT' }
    $stream = [IO.File]::Open($Path, 'Open', 'Read', 'ReadWrite')
    try {
        $bytes = New-Object byte[] 4
        if ($stream.Read($bytes, 0, 4) -ne 4) { return 'INVALID' }
        switch ([BitConverter]::ToString($bytes)) {
            'D4-C3-B2-A1' { return 'PCAP_MICROSECOND' }
            'A1-B2-C3-D4' { return 'PCAP_MICROSECOND' }
            '4D-3C-B2-A1' { return 'PCAP_NANOSECOND' }
            'A1-B2-3C-4D' { return 'PCAP_NANOSECOND' }
            '0A-0D-0D-0A' { return 'PCAPNG' }
            default { return 'INVALID' }
        }
    } finally { $stream.Dispose() }
}

function Test-HealthPass($Receipt) {
    return ($Receipt.Elevated -and $Receipt.ProcessTracked -and $Receipt.ConsoleAttached -and
        $Receipt.FileCreated -and $Receipt.GracefulQ -and $Receipt.ProcessExited -and
        $null -ne $Receipt.NativeExitCode -and $Receipt.NativeExitCode -eq 0 -and
        !$Receipt.ForcedCleanup -and $Receipt.Header -in @('PCAP_MICROSECOND','PCAP_NANOSECOND','PCAPNG') -and
        $Receipt.Bytes -gt 24 -and $Receipt.TsharkExitCode -eq 0 -and
        $Receipt.PacketCount -gt 0 -and $Receipt.TargetConfirmed -and $Receipt.TargetTrafficCount -gt 0)
}

function ConvertTo-PublicHealthReceipt($Receipt) {
    # Allowlist, never copy arbitrary keys/error text/path/PID/topology into public output.
    $out = [ordered]@{ Schema='usbpcap-launcher-health-public-1' }
    foreach ($key in @('Result','FailureCode','EvidenceKind','Elevated','ProcessTracked','ConsoleAttached','OutputPreExisted',
        'FileCreated','GracefulQ','ProcessExited','NativeExitCode','ForcedCleanup','Header','Bytes',
        'SHA256','TsharkExitCode','PacketCount','TargetConfirmed','TargetTrafficCount','DurationSeconds',
        'FileTimeoutSeconds','ExitTimeoutSeconds')) { $out[$key] = $Receipt[$key] }
    $out.States = @($Receipt.States | ForEach-Object { $_.State })
    $codes=@('ELEVATION_REQUIRED','EXECUTABLE_MISSING','INVALID_PATH','INVALID_DURATION','INVALID_TIMEOUT',
        'INVALID_SINGLE_TARGET','PRIVATE_PATH_REQUIRED','OUTPUT_PARENT_MISSING','REPARSE_PATH_REJECTED',
        'OUTPUT_RECEIPT_COLLISION','OUTPUT_ALREADY_EXISTS','CONSOLE_INPUT_UNAVAILABLE',
        'CONSOLE_CONTRACT_UNESTABLISHED','CONCURRENT_FORBIDDEN_ACTOR','EARLY_PROCESS_EXIT',
        'CONSOLE_ATTACH_UNCONFIRMED','FILE_CREATION_TIMEOUT','GRACEFUL_STOP_TIMEOUT','CLEANUP_FAILED',
        'CLEANUP_EXIT_TIMEOUT','RECEIPT_WRITE_FAILURE','INVALID_CAPTURE_HEADER','HEADER_ONLY_CAPTURE',
        'TSHARK_START_FAILED','TSHARK_TIMEOUT','TSHARK_PARSE_FAILED','NO_PACKETS','TARGET_NOT_CONFIRMED',
        'HEALTH_CONTRACT_NOT_MET','LAUNCHER_EXCEPTION')
    if ($out.FailureCode -and $out.FailureCode -notin $codes) { $out.FailureCode='LAUNCHER_EXCEPTION' }
    return $out
}

function Write-HealthReceipt($Receipt, [string]$Path) {
    # Receipt path is exclusively reserved by this run before any process launch.
    $temporary = $Path + '.update'
    [IO.File]::WriteAllText($temporary, ($Receipt | ConvertTo-Json -Depth 12), [Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $temporary -Destination $Path -Force
}

function Add-HealthState($Receipt, [string]$State, [string]$Path) {
    $Receipt.States += [ordered]@{ State=$State; TimestampUTC=[DateTime]::UtcNow.ToString('o') }
    Write-HealthReceipt $Receipt $Path
}

function Assert-PrivateHealthPath([string]$Path, [string]$Root) {
    $full = [IO.Path]::GetFullPath($Path)
    $base = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    if (!$full.StartsWith($base, [StringComparison]::OrdinalIgnoreCase)) { throw 'PRIVATE_PATH_REQUIRED' }
    $parent = [IO.DirectoryInfo]::new([IO.Path]::GetDirectoryName($full))
    if (!$parent.Exists) { throw 'OUTPUT_PARENT_MISSING' }
    while ($null -ne $parent) {
        if ($parent.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'REPARSE_PATH_REJECTED' }
        $parent = $parent.Parent
    }
    if ($full.Contains('"') -or $full.Contains("`n") -or $full.Contains("`r") -or $full.Length -gt 240) { throw 'INVALID_PATH' }
    return $full
}

function Start-HealthProcess([string]$Exe, [string]$Arguments) {
    $process=Start-Process -FilePath $Exe -ArgumentList $Arguments -NoNewWindow -PassThru
    $null=$process.Handle
    return $process
}

function Invoke-HealthOfflineParse($Config, [string]$Arguments, [string]$Suffix) {
    $info=[Diagnostics.ProcessStartInfo]::new()
    $info.FileName=$Config.TsharkPath; $info.Arguments=$Arguments; $info.UseShellExecute=$false
    $info.CreateNoWindow=$true; $info.RedirectStandardOutput=$true; $info.RedirectStandardError=$true
    $p=[Diagnostics.Process]::new(); $p.StartInfo=$info
    try {
        if (!$p.Start()) { throw 'TSHARK_START_FAILED' }
        $outTask=$p.StandardOutput.ReadToEndAsync(); $errTask=$p.StandardError.ReadToEndAsync()
        if (!$p.WaitForExit(30000)) { $p.Kill(); $null=$p.WaitForExit(5000); throw 'TSHARK_TIMEOUT' }
        $stdout=$outTask.GetAwaiter().GetResult(); $stderr=$errTask.GetAwaiter().GetResult()
        [IO.File]::WriteAllText($Config.OutputPath + '.' + $Suffix + '.txt', $stdout)
        [IO.File]::WriteAllText($Config.OutputPath + '.' + $Suffix + '.stderr.txt', $stderr)
        if ($p.ExitCode -ne 0) { throw 'TSHARK_PARSE_FAILED' }
        return @{ ExitCode=$p.ExitCode; Lines=@($stdout -split '\r?\n' | Where-Object { $_.Trim() }) }
    } finally { $p.Dispose() }
}

function Test-HealthCaptureFile($Config, $Receipt) {
    $Receipt.Header=Get-HealthHeader $Config.OutputPath
    if ($Receipt.Header -in @('INVALID','ABSENT')) { throw 'INVALID_CAPTURE_HEADER' }
    $file=Get-Item -LiteralPath $Config.OutputPath
    $Receipt.Bytes=$file.Length; $Receipt.FinalWriteTimeUTC=$file.LastWriteTimeUtc.ToString('o')
    $Receipt.SHA256=(Get-FileHash -LiteralPath $Config.OutputPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($file.Length -le 24) { throw 'HEADER_ONLY_CAPTURE' }
    $packets=Invoke-HealthOfflineParse $Config ('-n -r "{0}" -T fields -e frame.number' -f $Config.OutputPath) 'packets'
    $Receipt.TsharkExitCode=$packets.ExitCode; $Receipt.PacketCount=$packets.Lines.Count
    if ($Receipt.PacketCount -le 0) { throw 'NO_PACKETS' }
    # Match VID/PID descriptor to exactly the selected bus/address, then require real
    # non-descriptor USB traffic from the same tuple. Injected descriptors alone cannot pass.
    $tuple='usb.bus_id == {0} && usb.device_address == {1}' -f $Config.TargetBus,$Config.DeviceAddress
    $descriptor=Invoke-HealthOfflineParse $Config ('-n -r "{0}" -Y "({1}) && usb.idVendor == 0x0bb4 && usb.idProduct == 0x0350" -T fields -e frame.number' -f $Config.OutputPath,$tuple) 'descriptor'
    $traffic=Invoke-HealthOfflineParse $Config ('-n -r "{0}" -Y "({1}) && usb.transfer_type == 0x01 && usb.data_len > 0" -T fields -e frame.number' -f $Config.OutputPath,$tuple) 'target-traffic'
    $Receipt.TargetTrafficCount=$traffic.Lines.Count
    $Receipt.TargetConfirmed=($descriptor.Lines.Count -gt 0 -and $traffic.Lines.Count -gt 0)
    if (!$Receipt.TargetConfirmed) { throw 'TARGET_NOT_CONFIRMED' }
}

function New-HealthOperations {
    return @{
        Elevated={ [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) }
        ConsoleReady={ Initialize-HealthConsole; [UsbHealthConsole]::HasInput() -and @([UsbHealthConsole]::Members()).Count -eq 1 }
        Attached={ param($p) $ids=@([UsbHealthConsole]::Members()); $ids.Count -eq 2 -and $ids -contains $PID -and $ids -contains $p.Id }
        Start={ param($c,$a) Start-HealthProcess $c.USBPcapPath $a }
        Q={ [UsbHealthConsole]::SendQ() }
        Validate={ param($c,$r) Test-HealthCaptureFile $c $r }
    }
}

function Invoke-UsbHealthCapture($Config, [hashtable]$Operations=(New-HealthOperations)) {
    # Operations is an offline unit-test seam; the CLI never exposes overrides.
    $receiptPath=Assert-PrivateHealthPath $Config.ReceiptPath $Config.PrivateRoot
    if (Test-Path -LiteralPath $receiptPath) { throw 'RECEIPT_ALREADY_EXISTS' }
    $reserved=[IO.File]::Open($receiptPath,'CreateNew','Write','None'); $reserved.Dispose()
    $r=[ordered]@{ Schema='usbpcap-launcher-health-private-1'; Result='FAIL'; FailureCode=$null;
        LauncherPID=$PID; Elevated=$false; ProcessTracked=$false; ConsoleAttached=$false;
        USBPcapPID=$null; StartTimeUTC=$null; ExitTimeUTC=$null; InitialHasExited=$null;
        OutputPath=$Config.OutputPath; OutputPreExisted=$null; FileCreated=$false; GracefulQ=$false;
        ProcessExited=$false; NativeExitCode=$null; ForcedCleanup=$false; Header='NOT_RUN'; Bytes=$null;
        SHA256=$null; TsharkExitCode=$null; PacketCount=$null; TargetConfirmed=$false; TargetTrafficCount=$null;
        DurationSeconds=$Config.DurationSeconds; FileTimeoutSeconds=$Config.FileTimeoutSeconds;
        ExitTimeoutSeconds=$Config.ExitTimeoutSeconds; Arguments=$null; TargetBus=$Config.TargetBus;
        DeviceAddress=$Config.DeviceAddress; FinalWriteTimeUTC=$null; PrivateError=$null; States=@();
        EvidenceKind=$(if ($Config.OfflineTest) { 'OFFLINE_MOCK' } else { 'HARDWARE_CAPTURE' }) }
    $p=$null
    try {
        Add-HealthState $r 'Preflight' $receiptPath
        $r.Elevated=[bool](& $Operations.Elevated)
        if (!$r.Elevated) { throw 'ELEVATION_REQUIRED' }
        foreach ($exe in @($Config.USBPcapPath,$Config.TsharkPath)) {
            if (!(Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'EXECUTABLE_MISSING' }
            if ($exe.Contains('"') -or $exe.Contains("`n") -or $exe.Contains("`r")) { throw 'INVALID_PATH' }
        }
        if ($Config.DurationSeconds -lt 5 -or $Config.DurationSeconds -gt 10) { throw 'INVALID_DURATION' }
        if ($Config.FileTimeoutSeconds -lt 1 -or $Config.FileTimeoutSeconds -gt 10 -or
            $Config.ExitTimeoutSeconds -lt 1 -or $Config.ExitTimeoutSeconds -gt 10) { throw 'INVALID_TIMEOUT' }
        if ($Config.ControlDevice -notmatch '^\\\\\.\\USBPcap[1-9][0-9]{0,2}$' -or
            "$($Config.DeviceAddress)" -notmatch '^[1-9][0-9]{0,2}$' -or $Config.DeviceAddress -gt 127 -or
            $Config.TargetBus -lt 1 -or $Config.TargetBus -gt 65535) { throw 'INVALID_SINGLE_TARGET' }
        $Config.OutputPath=Assert-PrivateHealthPath $Config.OutputPath $Config.PrivateRoot
        if ($Config.OutputPath -eq $receiptPath) { throw 'OUTPUT_RECEIPT_COLLISION' }
        $r.OutputPreExisted=Test-Path -LiteralPath $Config.OutputPath
        if ($r.OutputPreExisted) { throw 'OUTPUT_ALREADY_EXISTS' }
        if (!(& $Operations.ConsoleReady)) { throw 'CONSOLE_INPUT_UNAVAILABLE' }
        if (!$Config.OfflineTest) {
            # A console membership list alone does not prove the GUI child's stdin
            # works. Require a PASS from the native GUI q/exit Run 0 with this module.
            $contractPath=Assert-PrivateHealthPath $Config.ConsoleContractReceiptPath $Config.PrivateRoot
            if (!(Test-Path -LiteralPath $contractPath -PathType Leaf)) { throw 'CONSOLE_CONTRACT_UNESTABLISHED' }
            $contract=Get-Content -LiteralPath $contractPath -Raw | ConvertFrom-Json
            $moduleHash=(Get-FileHash -LiteralPath $script:HealthModulePath -Algorithm SHA256).Hash
            if ($contract.Result -ne 'PASS' -or !$contract.Elevated -or !$contract.GracefulQ -or
                $contract.ForcedCleanup -or $contract.NativeExitCode -ne 23 -or
                $contract.ModuleSHA256 -ne $moduleHash -or $contract.EvidenceKind -ne 'OFFLINE_NATIVE_GUI_CHILD') {
                throw 'CONSOLE_CONTRACT_UNESTABLISHED'
            }
        }
        # Fail closed on concurrent capture / bridge / direct-HID actors. Inventory is read-only.
        if (!$Config.OfflineTest) {
            $actors=@(Get-CimInstance Win32_Process | Where-Object {
                $_.Name -match '^(USBPcapCMD|dumpcap|tshark|vut_.*|.*bridge.*|.*pyvut.*)\.exe$' -or
                ($_.Name -match '^python.*\.exe$' -and $_.CommandLine -match '(pyvut|vut_probe|direct.?hid|vive.*debug)') })
            $r.ActorInventory=@($actors | Select-Object ProcessId,Name,CommandLine)
            if ($actors.Count) { throw 'CONCURRENT_FORBIDDEN_ACTOR' }
        }
        $arguments='-d {0} --devices {1} -o "{2}"' -f $Config.ControlDevice,$Config.DeviceAddress,$Config.OutputPath
        if ($Config.InjectDescriptors) { $arguments+=' --inject-descriptors' }
        $r.Arguments=$arguments
        Add-HealthState $r 'Starting' $receiptPath
        $p=& $Operations.Start $Config $arguments
        $r.ProcessTracked=$true; $r.USBPcapPID=$p.Id; $r.StartTimeUTC=$p.StartTime.ToUniversalTime().ToString('o')
        $r.InitialHasExited=$p.HasExited
        Add-HealthState $r 'ProcessCreated' $receiptPath
        $watch=[Diagnostics.Stopwatch]::StartNew()
        while (!$p.HasExited -and !(& $Operations.Attached $p) -and $watch.Elapsed.TotalSeconds -lt 2) { Start-Sleep -Milliseconds 50 }
        if ($p.HasExited) { throw 'EARLY_PROCESS_EXIT' }
        $r.ConsoleAttached=[bool](& $Operations.Attached $p)
        if (!$r.ConsoleAttached) { throw 'CONSOLE_ATTACH_UNCONFIRMED' }
        Add-HealthState $r 'WaitingForFile' $receiptPath
        $watch.Restart()
        while (!(Test-Path -LiteralPath $Config.OutputPath) -and !$p.HasExited -and
            $watch.Elapsed.TotalSeconds -lt $Config.FileTimeoutSeconds) { Start-Sleep -Milliseconds 50 }
        $r.FileCreated=Test-Path -LiteralPath $Config.OutputPath -PathType Leaf
        if (!$r.FileCreated) { throw 'FILE_CREATION_TIMEOUT' }
        Add-HealthState $r 'Capturing' $receiptPath
        $watch.Restart()
        while (!$p.HasExited -and $watch.Elapsed.TotalSeconds -lt $Config.DurationSeconds) { Start-Sleep -Milliseconds 50 }
        if ($p.HasExited) { throw 'EARLY_PROCESS_EXIT' }
    } catch {
        $r.FailureCode=$_.Exception.Message
        $r.PrivateError=$_.ToString()
    } finally {
        if ($null -ne $p) {
            if (!$p.HasExited) {
                try {
                    if (!(& $Operations.Attached $p)) { throw 'CONSOLE_ATTACH_UNCONFIRMED' }
                    Add-HealthState $r 'StopRequested' $receiptPath
                    & $Operations.Q
                    $r.GracefulQ=$true; Write-HealthReceipt $r $receiptPath
                    if (!$p.WaitForExit([int]($Config.ExitTimeoutSeconds*1000))) { throw 'GRACEFUL_STOP_TIMEOUT' }
                } catch {
                    if (!$r.FailureCode) { $r.FailureCode=$_.Exception.Message }
                    $r.StopFailure=$_.Exception.Message
                }
            }
            if (!$p.HasExited) {
                try {
                    $r.Header=Get-HealthHeader $Config.OutputPath
                    if (Test-Path -LiteralPath $Config.OutputPath) { $r.Bytes=(Get-Item -LiteralPath $Config.OutputPath).Length }
                } catch { $r.FileInspectionError=$_.ToString() }
                $r.ForcedCleanup=$true
                try { Add-HealthState $r 'CleanupRequired' $receiptPath }
                catch { $r.FailureCode='RECEIPT_WRITE_FAILURE' }
                # Only the retained process handle; never kill by process name or arbitrary PID.
                try { $p.Kill(); $null=$p.WaitForExit(5000) }
                catch { $r.CleanupError=$_.ToString(); $r.FailureCode='CLEANUP_FAILED' }
            }
            if ($p.HasExited) {
                $r.ProcessExited=$true; $r.NativeExitCode=$p.ExitCode
                $r.ExitTimeUTC=$p.ExitTime.ToUniversalTime().ToString('o')
                Add-HealthState $r 'Exited' $receiptPath
            } else { $r.FailureCode='CLEANUP_EXIT_TIMEOUT' }
            $p.Dispose()
        }
    }
    try {
        if (!$r.FailureCode) {
            Add-HealthState $r 'Validating' $receiptPath
            & $Operations.Validate $Config $r
            if (!(Test-HealthPass $r)) { throw 'HEALTH_CONTRACT_NOT_MET' }
            $r.Result='PASS'
        }
    } catch { $r.FailureCode=$_.Exception.Message; $r.PrivateError=$_.ToString() }
    # Do not publish exception strings. Allowlist failure codes too.
    if ($r.FailureCode -and $r.FailureCode -notmatch '^[A-Z][A-Z_]+$') { $r.FailureCode='LAUNCHER_EXCEPTION' }
    Add-HealthState $r $(if ($r.Result -eq 'PASS') { 'Succeeded' } else { 'Failed' }) $receiptPath
    return $r
}

Export-ModuleMember -Function Initialize-HealthConsole,Get-HealthHeader,Test-HealthPass,ConvertTo-PublicHealthReceipt,Invoke-UsbHealthCapture,Start-HealthProcess
