# Offline tests: no USB device access. Mock PASS is never hardware evidence.
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot '../scripts/usbpcap_health_contract.psm1') -Force
$testRoot=Join-Path ([IO.Path]::GetTempPath()) ('usbhealth-tests-'+[Guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $testRoot
$script:checks=0
function Check($value,$message) { if (!$value) { throw $message }; $script:checks++ }
function Config {
    $output=Join-Path $testRoot ([Guid]::NewGuid().ToString('N')+'.pcap')
    return @{ USBPcapPath=(Get-Process -Id $PID).Path; TsharkPath=(Get-Process -Id $PID).Path;
        ControlDevice='\\.\USBPcap1'; DeviceAddress='1'; TargetBus=1; OutputPath=$output;
        ReceiptPath=$output+'.receipt.json'; PrivateRoot=$testRoot; DurationSeconds=5;
        FileTimeoutSeconds=1; ExitTimeoutSeconds=1; InjectDescriptors=$true; OfflineTest=$true }
}
function Fake($timeout=$false) {
    $p=[pscustomobject]@{ Id=1234; StartTime=[DateTime]::UtcNow; ExitTime=[DateTime]::UtcNow;
        HasExited=$false; ExitCode=23; Timeout=$timeout; Killed=$false }
    $p | Add-Member ScriptMethod WaitForExit { param($milliseconds) if ($this.Timeout -and !$this.Killed) { return $false }; $this.HasExited=$true; return $true }
    $p | Add-Member ScriptMethod Kill { $this.Killed=$true; $this.HasExited=$true }
    $p | Add-Member ScriptMethod Dispose { }
    return $p
}
function Ops {
    return @{ Elevated={ $true }; ConsoleReady={ $true }; Attached={ $true };
        Start={ param($c,$a) [IO.File]::WriteAllBytes($c.OutputPath, [byte[]](0xd4,0xc3,0xb2,0xa1)+[byte[]]::new(30)); Fake };
        Q={ }; Validate={ param($c,$r) $r.Header='PCAP_MICROSECOND'; $r.Bytes=34;
            $r.TsharkExitCode=0; $r.PacketCount=3; $r.TargetTrafficCount=1; $r.TargetConfirmed=$true } }
}
function Rejection($mutate,$expected,$operations=$null) {
    $c=Config; & $mutate $c
    if ($null -eq $operations) { $operations=Ops }
    $operations.Start={ throw 'TEST_UNEXPECTED_CHILD_START' }
    $r=Invoke-UsbHealthCapture $c $operations
    Check ($r.Result -eq 'FAIL' -and $r.FailureCode -eq $expected) $expected
    Check (!$r.ProcessTracked) 'Rejected preflight launched a child'
    Check (Test-Path $c.ReceiptPath) 'Missing failure receipt'
}
$op=Ops; $op.Elevated={ $false }; Rejection { param($c) } 'ELEVATION_REQUIRED' $op
Rejection { param($c) $c.USBPcapPath=Join-Path $testRoot 'missing.exe' } 'EXECUTABLE_MISSING'
Rejection { param($c) [IO.File]::WriteAllText($c.OutputPath,'sentinel') } 'OUTPUT_ALREADY_EXISTS'
foreach ($duration in @(0,4,11,2147483647)) {
    Rejection { param($c) $c.DurationSeconds=$duration } 'INVALID_DURATION'
}
foreach ($address in @('0','128','1,2','1 --capture-from-all-devices')) {
    Rejection { param($c) $c.DeviceAddress=$address } 'INVALID_SINGLE_TARGET'
}
$op=Ops; $op.ConsoleReady={ $false }; Rejection { param($c) } 'CONSOLE_INPUT_UNAVAILABLE' $op
Rejection { param($c) $c.OfflineTest=$false; $c.ConsoleContractReceiptPath=Join-Path $testRoot 'absent-contract.json' } 'CONSOLE_CONTRACT_UNESTABLISHED'
Rejection { param($c) $c.OfflineTest=$false; $c.ConsoleContractReceiptPath=Join-Path $testRoot 'failed-contract.json';
    [IO.File]::WriteAllText($c.ConsoleContractReceiptPath,'{"Result":"FAIL"}') } 'CONSOLE_CONTRACT_UNESTABLISHED'

$c=Config; $op=Ops
$op.Start={ param($c,$a) Fake }
$r=Invoke-UsbHealthCapture $c $op
Check ($r.FailureCode -eq 'FILE_CREATION_TIMEOUT') 'File timeout not diagnosed'
Check ($r.ProcessTracked -and $r.GracefulQ -and $r.ProcessExited -and $r.NativeExitCode -eq 23) 'Missing tracked native exit on file timeout'
Check (!$r.ForcedCleanup) 'Normal exit was forced'
Check (($r.States | ForEach-Object State) -contains 'Exited') 'Exited state missing'

$c=Config; $op=Ops
$op.Start={ param($c,$a) Fake $true }
$r=Invoke-UsbHealthCapture $c $op
Check ($r.Result -eq 'FAIL' -and $r.ForcedCleanup -and $r.StopFailure -eq 'GRACEFUL_STOP_TIMEOUT') 'Timeout cleanup falsely passed'
Check ($r.NativeExitCode -eq 23) 'Cleanup native exit missing'
Check (($r.States | ForEach-Object State) -contains 'CleanupRequired') 'Cleanup state missing'

$c=Config; $op=Ops
$op.Start={ param($c,$a) $p=Fake; $p.HasExited=$true; return $p }
$r=Invoke-UsbHealthCapture $c $op
Check ($r.FailureCode -eq 'EARLY_PROCESS_EXIT' -and !$r.GracefulQ) 'Early exit falsely passed'

# Real OS process object: retain handle and obtain a deliberately nonzero native exit.
$real=Start-HealthProcess (Get-Process -Id $PID).Path '-NoProfile -NonInteractive -Command "exit 23"'
try { Check ($real.Id -gt 0) 'Real child PID absent'; Check ($real.WaitForExit(10000)) 'Real child did not exit'; Check ($real.ExitCode -eq 23) 'Real native exit incorrect' }
finally { if (!$real.HasExited) { $real.Kill(); $null=$real.WaitForExit(5000) }; $real.Dispose() }
$real=Start-HealthProcess (Get-Process -Id $PID).Path '-NoProfile -NonInteractive -Command "Start-Sleep -Seconds 30"'
try { Check (!$real.WaitForExit(100)) 'Timeout child unexpectedly exited' }
finally { if (!$real.HasExited) { $real.Kill(); $null=$real.WaitForExit(5000) }; $real.Dispose() }

$c=Config; $op=Ops; $op.Q={ }; $op.Start={ param($c,$a)
    [IO.File]::WriteAllBytes($c.OutputPath,[byte[]](0xd4,0xc3,0xb2,0xa1)+[byte[]]::new(30)); $p=Fake; $p.ExitCode=0; return $p }
$r=Invoke-UsbHealthCapture $c $op
Check ($r.Result -eq 'PASS' -and $r.EvidenceKind -eq 'OFFLINE_MOCK') 'Mock lifecycle failed or missing mock label'
Check ((($r.States | ForEach-Object State) -join ',') -eq 'Preflight,Starting,ProcessCreated,WaitingForFile,Capturing,StopRequested,Exited,Validating,Succeeded') 'Lifecycle ordering incorrect'
$r.PrivateError='C:\private\secret-serial RF USB topology'; $r.Serial='SECRET_SERIAL'
$public=ConvertTo-PublicHealthReceipt $r | ConvertTo-Json -Depth 6
Check ($public -notmatch 'secret|1234|OutputPath|LauncherPID|TargetBus|DeviceAddress|Timestamp|Arguments|private') 'Public receipt leaks raw values'
$r.FailureCode='SECRET_SERIAL'; $public=ConvertTo-PublicHealthReceipt $r | ConvertTo-Json -Depth 6
Check ($public -notmatch 'SECRET_SERIAL') 'Failure code leaked private identifier'
$r.TargetConfirmed=$false; Check (!(Test-HealthPass $r)) 'Unconfirmed target passed'
$r.TargetConfirmed=$true; $r.TargetTrafficCount=0; Check (!(Test-HealthPass $r)) 'Descriptor-only passed'
$r.TargetTrafficCount=1; $r.NativeExitCode=$null; Check (!(Test-HealthPass $r)) 'Unknown exit passed'
$r.NativeExitCode=7; Check (!(Test-HealthPass $r)) 'Nonzero exit passed'
$r.NativeExitCode=0; $r.ForcedCleanup=$true; Check (!(Test-HealthPass $r)) 'Forced termination passed'

foreach ($magic in @('D4C3B2A1','A1B2C3D4','4D3CB2A1','A1B23C4D','0A0D0D0A','00000000','12345678')) {
    $path=Join-Path $testRoot ($magic+'.bin')
    $bytes=[byte[]]@(for ($i=0; $i -lt 8; $i+=2) { [Convert]::ToByte($magic.Substring($i,2),16) })
    [IO.File]::WriteAllBytes($path,$bytes)
    $header=Get-HealthHeader $path
    Check (($header -ne 'INVALID') -eq ($magic -notin @('00000000','12345678'))) 'Capture magic validation failed'
}
[IO.File]::WriteAllBytes((Join-Path $testRoot 'short.bin'),[byte[]](1,2,3))
Check ((Get-HealthHeader (Join-Path $testRoot 'short.bin')) -eq 'INVALID') 'Truncated header passed'
Write-Output "PASS: $script:checks offline assertions; no USB device access; mock results are not Health Gate evidence."
