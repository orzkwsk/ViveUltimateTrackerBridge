param([Parameter(Mandatory=$true)][string]$PrivateDirectory, [string]$NativeChildPath, [switch]$UseStartProcess)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot '../scripts/usbpcap_health_contract.psm1') -Force
Initialize-HealthConsole
$receipt=@{ EvidenceKind=$(if ($NativeChildPath) { 'OFFLINE_NATIVE_GUI_CHILD' } else { 'OFFLINE_MANAGED_GUI_CHILD' }); Elevated=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator); ConsoleInput=[UsbHealthConsole]::HasInput(); Result='FAIL'; GracefulQ=$false; ForcedCleanup=$false; NativeExitCode=$null; ModuleSHA256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot '../scripts/usbpcap_health_contract.psm1') -Algorithm SHA256).Hash }
$p=$null
$receipt.States=@(@{State='Preflight';TimestampUTC=[DateTime]::UtcNow.ToString('o')})
try {
    if (!$receipt.Elevated -or !$receipt.ConsoleInput -or @([UsbHealthConsole]::Members()).Count -ne 1) { throw 'DRY_CONSOLE_PREFLIGHT_FAILED' }
    $exe=$NativeChildPath
    if (!$exe) {
        $exe=Join-Path $PrivateDirectory ('console-child-'+[Guid]::NewGuid().ToString('N')+'.exe')
        Add-Type -Path (Join-Path $PSScriptRoot 'usbpcap_console_child.cs') -OutputAssembly $exe -OutputType WindowsApplication
    }
    $inputReceipt=Join-Path $PrivateDirectory 'gui-child-input.txt'
    $receipt.States+=@{State='Starting';TimestampUTC=[DateTime]::UtcNow.ToString('o')}
    $childArguments=$(if ($NativeChildPath) { $inputReceipt } else { '"{0}"' -f $inputReceipt })
    if ($UseStartProcess) { $p=Start-Process -FilePath $exe -ArgumentList $childArguments -NoNewWindow -PassThru; $null=$p.Handle }
    else { $p=Start-HealthProcess $exe $childArguments }
    $receipt.ChildPID=$p.Id
    $receipt.InitialHasExited=$p.HasExited
    $receipt.StartTimeUTC=$p.StartTime.ToUniversalTime().ToString('o')
    $receipt.States+=@{State='ProcessCreated';TimestampUTC=[DateTime]::UtcNow.ToString('o')}
    $watch=[Diagnostics.Stopwatch]::StartNew()
    while (!$p.HasExited -and @([UsbHealthConsole]::Members()) -notcontains $p.Id -and $watch.Elapsed.TotalSeconds -lt 3) { Start-Sleep -Milliseconds 50 }
    $receipt.ConsoleMembers=@([UsbHealthConsole]::Members())
    if ($p.HasExited -or $receipt.ConsoleMembers.Count -ne 2 -or $receipt.ConsoleMembers -notcontains $p.Id) { throw 'DRY_ATTACH_FAILED' }
    # Wait until the child has checked its inherited STD_INPUT_HANDLE.
    $watch.Restart()
    while (!(Test-Path $inputReceipt) -and !$p.HasExited -and $watch.Elapsed.TotalSeconds -lt 3) { Start-Sleep -Milliseconds 50 }
    if (!(Test-Path $inputReceipt) -or (Get-Content $inputReceipt -Raw) -notmatch 'after=True') { throw 'DRY_INPUT_FAILED' }
    [UsbHealthConsole]::SendQ(); $receipt.GracefulQ=$true
    if (!$p.WaitForExit(10000)) { throw 'DRY_Q_TIMEOUT' }
    $receipt.NativeExitCode=$p.ExitCode
    if ($p.ExitCode -ne 23) { throw 'DRY_EXIT_FAILED' }
    $receipt.Result='PASS'
} catch { $receipt.Error=$_.ToString() }
finally {
    if ($null -ne $p) {
        if (!$p.HasExited) { $receipt.ForcedCleanup=$true; $p.Kill(); $null=$p.WaitForExit(5000) }
        if ($p.HasExited) { $receipt.NativeExitCode=$p.ExitCode; $receipt.ProcessExited=$true;
            $receipt.ExitTimeUTC=$p.ExitTime.ToUniversalTime().ToString('o');
            $receipt.States+=@{State='Exited';TimestampUTC=[DateTime]::UtcNow.ToString('o')} }
        $p.Dispose()
    }
    $receipt.States+=@{State=$(if ($receipt.Result -eq 'PASS') { 'Succeeded' } else { 'Failed' });TimestampUTC=[DateTime]::UtcNow.ToString('o')}
    $receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $PrivateDirectory 'console-smoke-private.json') -Encoding UTF8
}
if ($receipt.Result -ne 'PASS') { exit 1 }; exit 0
