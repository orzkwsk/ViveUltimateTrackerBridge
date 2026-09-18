[CmdletBinding()]
param(
    [string]$ConfigPath = (Join-Path $PSScriptRoot 'config.json')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:State = 'INIT'
$script:Result = 'running'
$script:FailureMessage = $null
$script:StartTime = $null
$script:EndTime = $null
$script:RunId = $null
$script:RunDirectory = $null
$script:EventsPath = $null
$script:HarnessLogPath = $null
$script:Events = New-Object System.Collections.ArrayList
$script:LoggerProcesses = New-Object System.Collections.ArrayList
$script:ApplicationProcess = $null
$script:ApplicationStopInfo = $null
$script:RepositoryInfo = $null
$script:Config = $null
$script:ConfigBase = $null
$script:RunsDirectory = $null
$script:PackageStatus = 'not-requested'
$script:LoggingActivated = $false
$script:DongleWatchJob = $null
$script:DeviceWatchPath = $null
$script:ApplicationFileInfo = $null
$script:HarnessScriptSha256 = $null

function Get-IsoTimestamp {
    return (Get-Date).ToString('yyyy-MM-ddTHH:mm:ss.fffK')
}

function Resolve-ConfiguredPath {
    param([string]$PathValue, [string]$BaseDirectory)
    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return [System.IO.Path]::GetFullPath($PathValue)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $PathValue))
}

function Write-HarnessLog {
    param([string]$Message, [ValidateSet('INFO','WARN','ERROR')][string]$Level = 'INFO')
    $line = '{0} [{1}] [{2}] {3}' -f (Get-IsoTimestamp), $Level, $script:State, $Message
    Write-Host $line
    if ($script:HarnessLogPath) {
        Add-Content -LiteralPath $script:HarnessLogPath -Value $line -Encoding UTF8
    }
}

function Add-HarnessEvent {
    param([string]$Event, [string]$Source, [hashtable]$Details)
    $record = [ordered]@{
        timestamp = Get-IsoTimestamp
        event = $Event
        source = $Source
        state = $script:State
    }
    if ($Details) {
        foreach ($key in $Details.Keys) {
            $record[$key] = $Details[$key]
        }
    }
    $record | ConvertTo-Json -Compress -Depth 12 |
        Add-Content -LiteralPath $script:EventsPath -Encoding UTF8
    [void]$script:Events.Add([pscustomobject]$record)
}

function Set-HarnessState {
    param([string]$NewState)
    $old = $script:State
    $script:State = $NewState
    Write-HarnessLog ('STATE {0} -> {1}' -f $old, $NewState)
    Add-HarnessEvent -Event 'StateChanged' -Source 'harness' -Details @{ from = $old; to = $NewState }
}

function New-RunId {
    param([string]$RunsDirectory)
    $datePart = (Get-Date).ToString('yyyyMMdd')
    $timePart = (Get-Date).ToString('HHmmss')
    $maxSequence = 0
    if (Test-Path -LiteralPath $RunsDirectory) {
        Get-ChildItem -LiteralPath $RunsDirectory -Directory -ErrorAction SilentlyContinue | ForEach-Object {
            if ($_.Name -match ('^{0}_[0-9]{{6}}_([0-9]{{3}})' -f $datePart)) {
                $value = [int]$matches[1]
                if ($value -gt $maxSequence) { $maxSequence = $value }
            }
        }
    }
    return '{0}_{1}_{2:D3}' -f $datePart, $timePart, ($maxSequence + 1)
}

function Invoke-Git {
    param([string]$RepositoryPath, [string[]]$Arguments, [switch]$ThrowOnFailure)
    Write-HarnessLog ('git ' + ($Arguments -join ' '))
    Push-Location $RepositoryPath
    try {
        $previous = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            $output = (& git @Arguments 2>&1 | Out-String).TrimEnd()
            $exitCode = $LASTEXITCODE
        }
        finally {
            $ErrorActionPreference = $previous
        }
    }
    finally {
        Pop-Location
    }
    if ($output) {
        Write-HarnessLog ('git output: ' + ($output -replace '[\r\n]+', ' | '))
    }
    if ($ThrowOnFailure -and $exitCode -ne 0) {
        throw "git $($Arguments -join ' ') failed with exit code $exitCode"
    }
    return [pscustomobject]@{ ExitCode = $exitCode; Output = $output }
}

function Get-RepositorySnapshot {
    param([string]$RepositoryPath)
    $head = Invoke-Git $RepositoryPath @('rev-parse','HEAD') -ThrowOnFailure
    $short = Invoke-Git $RepositoryPath @('rev-parse','--short=12','HEAD') -ThrowOnFailure
    $branch = Invoke-Git $RepositoryPath @('branch','--show-current') -ThrowOnFailure
    $status = Invoke-Git $RepositoryPath @('status','--porcelain') -ThrowOnFailure
    $remote = Invoke-Git $RepositoryPath @('remote','get-url',[string]$script:Config.repository.remote)
    return [pscustomobject]@{
        path = $RepositoryPath
        remoteUrl = $remote.Output
        branch = $branch.Output.Trim()
        commit = $head.Output.Trim()
        shortCommit = $short.Output.Trim()
        dirty = -not [string]::IsNullOrWhiteSpace($status.Output)
        status = $status.Output
    }
}

function Get-DongleDevices {
    param([switch]$PresentOnly)
    if (-not (Get-Command Get-PnpDevice -ErrorAction SilentlyContinue)) {
        throw 'Get-PnpDevice is unavailable. PnpDevice module is required.'
    }
    if ($PresentOnly) {
        $devices = @(Get-PnpDevice -PresentOnly -ErrorAction Stop)
    } else {
        $devices = @(Get-PnpDevice -ErrorAction SilentlyContinue)
    }
    return @($devices |
        Where-Object { $_.InstanceId -like [string]$script:Config.dongle.instanceIdPattern } |
        ForEach-Object {
            [pscustomobject]@{
                FriendlyName = $_.FriendlyName
                InstanceId = $_.InstanceId
                Status = $_.Status
                Class = $_.Class
            }
        })
}

function Save-DeviceSnapshot {
    param([string]$Path)
    $devices = @(Get-DongleDevices)
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add(('timestamp={0}' -f (Get-IsoTimestamp)))
    $lines.Add(('pattern={0}' -f $script:Config.dongle.instanceIdPattern))
    if ($devices.Count -eq 0) {
        $lines.Add('<no matching PnP devices>')
    } else {
        foreach ($device in $devices) {
            $lines.Add('')
            $lines.Add(('FriendlyName={0}' -f $device.FriendlyName))
            $lines.Add(('InstanceId={0}' -f $device.InstanceId))
            $lines.Add(('Status={0}' -f $device.Status))
            $lines.Add(('Class={0}' -f $device.Class))
        }
    }
    Set-Content -LiteralPath $Path -Value $lines -Encoding UTF8
}

function Wait-ForDongleAbsent {
    $announced = $false
    while ($true) {
        if (@(Get-DongleDevices -PresentOnly).Count -eq 0) { return }
        if (-not $announced) {
            Write-Host ''
            Write-Host '対象ドングルを抜いてください。'
            Write-HarnessLog 'Dongle present during PRECHECK; waiting for removal.' 'WARN'
            $announced = $true
        }
        Start-Sleep -Milliseconds ([int]$script:Config.dongle.pollIntervalMs)
    }
}

function Wait-ForDonglePresent {
    $timeoutMs = [int]$script:Config.timeouts.dongleConnectMs
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    Write-Host ''
    Write-Host '対象ドングルを挿入してください。'
    while ($true) {
        $present = @(Get-DongleDevices -PresentOnly)
        if ($present.Count -gt 0) {
            Add-HarnessEvent -Event 'DongleConnected' -Source 'device' -Details @{ device = $present[0].InstanceId }
            Write-HarnessLog ('Dongle connected: ' + $present[0].InstanceId)
            return
        }
        if ($timeoutMs -gt 0 -and $watch.ElapsedMilliseconds -ge $timeoutMs) {
            throw "Dongle connection timed out after $timeoutMs ms."
        }
        Start-Sleep -Milliseconds ([int]$script:Config.dongle.pollIntervalMs)
    }
}

function Start-DongleWatcher {
    if ($script:DongleWatchJob) { return }

    $script:DeviceWatchPath = Join-Path $script:RunDirectory 'device_watch.jsonl'
    New-Item -ItemType File -Path $script:DeviceWatchPath -Force | Out-Null

    $pattern = [string]$script:Config.dongle.instanceIdPattern
    $interval = [int]$script:Config.dongle.pollIntervalMs
    $outputPath = $script:DeviceWatchPath

    $script:DongleWatchJob = Start-Job -ArgumentList $pattern, $interval, $outputPath -ScriptBlock {
        param($Pattern, $IntervalMs, $OutputPath)

        function Get-Present {
            try {
                return (@(Get-PnpDevice -PresentOnly -ErrorAction Stop |
                    Where-Object { $_.InstanceId -like $Pattern }).Count -gt 0)
            }
            catch {
                return $null
            }
        }

        $last = Get-Present
        while ($true) {
            Start-Sleep -Milliseconds $IntervalMs
            $present = Get-Present
            if ($null -eq $present) { continue }

            if ($null -ne $last -and $present -ne $last) {
                $eventName = $(if ($present) { 'DongleConnected' } else { 'DongleDisconnected' })
                $record = [ordered]@{
                    timestamp = (Get-Date).ToString('yyyy-MM-ddTHH:mm:ss.fffK')
                    event = $eventName
                    source = 'device'
                    state = 'ASYNC_MONITOR'
                }
                $record | ConvertTo-Json -Compress |
                    Add-Content -LiteralPath $OutputPath -Encoding UTF8
            }
            $last = $present
        }
    }

    Write-HarnessLog ('Dongle background watcher started, JobId=' + $script:DongleWatchJob.Id)
}

function Stop-DongleWatcher {
    if (-not $script:DongleWatchJob) { return }
    try {
        Stop-Job -Job $script:DongleWatchJob -ErrorAction SilentlyContinue
        Receive-Job -Job $script:DongleWatchJob -ErrorAction SilentlyContinue | Out-Null
        Remove-Job -Job $script:DongleWatchJob -Force -ErrorAction SilentlyContinue
    }
    catch {
        Write-HarnessLog ('Failed to stop dongle watcher: ' + $_.Exception.Message) 'WARN'
    }
    finally {
        $script:DongleWatchJob = $null
    }
}

function Merge-DeviceEvents {
    if (-not $script:DeviceWatchPath -or -not (Test-Path -LiteralPath $script:DeviceWatchPath)) { return }

    $all = New-Object System.Collections.ArrayList

    foreach ($path in @($script:EventsPath, $script:DeviceWatchPath)) {
        foreach ($line in @(Get-Content -LiteralPath $path -ErrorAction SilentlyContinue)) {
            if ([string]::IsNullOrWhiteSpace($line)) { continue }
            try {
                [void]$all.Add(($line | ConvertFrom-Json))
            }
            catch {
                Write-HarnessLog ('Ignoring malformed event line from ' + $path) 'WARN'
            }
        }
    }

    $sorted = @($all | Sort-Object { [DateTimeOffset]::Parse($_.timestamp) })
    Set-Content -LiteralPath $script:EventsPath -Value @() -Encoding UTF8
    foreach ($record in $sorted) {
        $record | ConvertTo-Json -Compress -Depth 12 |
            Add-Content -LiteralPath $script:EventsPath -Encoding UTF8
    }

    $script:Events = New-Object System.Collections.ArrayList
    foreach ($record in $sorted) { [void]$script:Events.Add($record) }
}

function Assert-NoResidualProcesses {
    $names = New-Object System.Collections.Generic.List[string]
    if ($script:Config.application.residualProcessNames) {
        foreach ($name in $script:Config.application.residualProcessNames) {
            if ($name) { $names.Add([string]$name) }
        }
    }
    if ($script:Config.logging.processes) {
        foreach ($logger in $script:Config.logging.processes) {
            if ($logger.enabled -and $logger.processName) { $names.Add([string]$logger.processName) }
        }
    }
    $found = New-Object System.Collections.Generic.List[string]
    foreach ($name in ($names | Select-Object -Unique)) {
        foreach ($process in @(Get-Process -Name $name -ErrorAction SilentlyContinue)) {
            $found.Add(('{0}(PID={1})' -f $process.ProcessName, $process.Id))
        }
    }
    if ($found.Count -gt 0) {
        throw ('Residual target/logger processes are running: ' + ($found -join ', '))
    }
}

function Expand-LoggerArgument {
    param([string]$Value)
    $expanded = $Value
    $expanded = $expanded.Replace('{runDir}', $script:RunDirectory)
    $expanded = $expanded.Replace('{runId}', $script:RunId)
    return $expanded
}

function Start-ExternalLoggers {
    if (-not $script:Config.logging.processes) {
        Write-HarnessLog 'No external logging processes configured.'
        return
    }
    foreach ($logger in $script:Config.logging.processes) {
        if (-not $logger.enabled) { continue }
        $exe = Resolve-ConfiguredPath ([string]$logger.executable) $script:ConfigBase
        if (-not (Test-Path -LiteralPath $exe)) { throw "Logger executable not found: $exe" }
        $workingDirectory = $script:ConfigBase
        if ($logger.workingDirectory) {
            $workingDirectory = Resolve-ConfiguredPath ([string]$logger.workingDirectory) $script:ConfigBase
        }
        $arguments = @()
        if ($logger.arguments) {
            foreach ($arg in $logger.arguments) { $arguments += (Expand-LoggerArgument ([string]$arg)) }
        }
        $startArgs = @{
            FilePath = $exe
            WorkingDirectory = $workingDirectory
            PassThru = $true
        }
        if ($arguments.Count -gt 0) { $startArgs.ArgumentList = $arguments }
        if ($logger.stdoutFile) { $startArgs.RedirectStandardOutput = Join-Path $script:RunDirectory ([string]$logger.stdoutFile) }
        if ($logger.stderrFile) { $startArgs.RedirectStandardError = Join-Path $script:RunDirectory ([string]$logger.stderrFile) }
        $process = Start-Process @startArgs
        [void]$script:LoggerProcesses.Add([pscustomobject]@{
            Name = [string]$logger.name
            Kind = [string]$logger.kind
            Process = $process
            StopMode = [string]$logger.stopMode
            ForcedStop = $false
            StopError = $null
        })
        Add-HarnessEvent -Event 'LoggerStarted' -Source 'harness' -Details @{ message = [string]$logger.name; pid = $process.Id }
        Write-HarnessLog ("Logger started: {0}, PID={1}" -f $logger.name, $process.Id)
    }
}

function Start-TargetApplication {
    if ($script:ApplicationProcess) { return }
    $exe = Resolve-ConfiguredPath ([string]$script:Config.application.path) $script:ConfigBase
    if (-not (Test-Path -LiteralPath $exe)) { throw "Application executable not found: $exe" }
    $workingDirectory = Split-Path -Parent $exe
    if ($script:Config.application.workingDirectory) {
        $workingDirectory = Resolve-ConfiguredPath ([string]$script:Config.application.workingDirectory) $script:ConfigBase
    }
    $arguments = @()
    if ($script:Config.application.arguments) {
        foreach ($arg in $script:Config.application.arguments) { $arguments += [string]$arg }
    }
    $startArgs = @{
        FilePath = $exe
        WorkingDirectory = $workingDirectory
        RedirectStandardOutput = (Join-Path $script:RunDirectory 'application.log')
        RedirectStandardError = (Join-Path $script:RunDirectory 'application.err.log')
        PassThru = $true
    }
    if ($arguments.Count -gt 0) { $startArgs.ArgumentList = $arguments }
    $file = Get-Item -LiteralPath $exe
    $hash = Get-FileHash -LiteralPath $exe -Algorithm SHA256
    $script:ApplicationFileInfo = [pscustomobject]@{
        path = $file.FullName
        sha256 = $hash.Hash
        length = $file.Length
        mtimeUtc = $file.LastWriteTimeUtc.ToString('o')
    }

    $script:ApplicationProcess = Start-Process @startArgs
    Start-Sleep -Milliseconds ([int]$script:Config.timeouts.applicationStartMs)
    if ($script:ApplicationProcess.HasExited) {
        throw "Application exited during startup with code $($script:ApplicationProcess.ExitCode)."
    }
    Add-HarnessEvent -Event 'ApplicationStarted' -Source 'harness' -Details @{ pid = $script:ApplicationProcess.Id; message = $exe }
    Write-HarnessLog ("Application started: {0}, PID={1}" -f $exe, $script:ApplicationProcess.Id)
}

function Stop-ManagedProcess {
    param($Record, [int]$TimeoutMs = 5000)
    $process = $Record.Process
    if ($null -eq $process) { return }
    try {
        $process.Refresh()
        if ($process.HasExited) { return }
        if ([string]$Record.StopMode -eq 'CloseMainWindow') {
            $closed = $false
            try { $closed = $process.CloseMainWindow() } catch { $closed = $false }
            if ($closed) {
                [void]$process.WaitForExit($TimeoutMs)
                $process.Refresh()
            }
        }
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction Stop
            $Record.ForcedStop = $true
            [void]$process.WaitForExit($TimeoutMs)
        }
    } catch {
        $Record.StopError = $_.Exception.Message
        Write-HarnessLog ("Failed to stop PID={0}: {1}" -f $process.Id, $_.Exception.Message) 'WARN'
    }
}

function Stop-LoggingAndApplication {
    Stop-DongleWatcher

    $captures = @($script:LoggerProcesses | Where-Object { $_.Kind -eq 'capture' })
    for ($i = $captures.Count - 1; $i -ge 0; $i--) {
        Stop-ManagedProcess $captures[$i] ([int]$script:Config.timeouts.loggerStopMs)
    }
    Start-Sleep -Milliseconds ([int]$script:Config.logging.flushWaitMs)
    $others = @($script:LoggerProcesses | Where-Object { $_.Kind -ne 'capture' })
    for ($i = $others.Count - 1; $i -ge 0; $i--) {
        Stop-ManagedProcess $others[$i] ([int]$script:Config.timeouts.loggerStopMs)
    }
    if ($script:ApplicationProcess -and $script:Config.application.stopOnComplete) {
        $record = [pscustomobject]@{
            Name = 'application'
            Kind = 'application'
            Process = $script:ApplicationProcess
            StopMode = [string]$script:Config.application.stopMode
            ForcedStop = $false
            StopError = $null
        }
        Stop-ManagedProcess $record ([int]$script:Config.timeouts.loggerStopMs)
        $script:ApplicationStopInfo = $record
    }
    if ($script:LoggingActivated) {
        Add-HarnessEvent -Event 'LoggingStopped' -Source 'harness' -Details @{}
        $script:LoggingActivated = $false
    }
}

function Wait-Operator {
    param([string]$Prompt, [string]$CompletedEvent)
    Write-Host ''
    Write-Host $Prompt
    Write-Host 'Enter = 完了'
    Write-Host 'F     = Failed'
    Write-Host 'S     = Skip'
    Write-Host 'Q     = Abort'
    while ($true) {
        $value = Read-Host
        if ([string]::IsNullOrWhiteSpace($value)) {
            Add-HarnessEvent -Event $CompletedEvent -Source 'operator' -Details @{ result = 'completed' }
            return 'completed'
        }
        switch ($value.Trim().ToUpperInvariant()) {
            'F' {
                Add-HarnessEvent -Event ($CompletedEvent + 'Failed') -Source 'operator' -Details @{ result = 'failed' }
                return 'failed'
            }
            'S' {
                Add-HarnessEvent -Event ($CompletedEvent + 'Skipped') -Source 'operator' -Details @{ result = 'skipped' }
                return 'skipped'
            }
            'Q' {
                Add-HarnessEvent -Event 'OperatorAbort' -Source 'operator' -Details @{ message = $CompletedEvent; result = 'aborted' }
                return 'aborted'
            }
            default { Write-Host 'Enter / F / S / Q のいずれかを入力してください。' }
        }
    }
}

function Save-EnvironmentInfo {
    $lines = @(
        ('timestamp={0}' -f (Get-IsoTimestamp)),
        ('computerName={0}' -f [System.Environment]::MachineName),
        ('userName={0}' -f [System.Environment]::UserName),
        ('osVersion={0}' -f [System.Environment]::OSVersion.VersionString),
        ('is64BitOS={0}' -f [System.Environment]::Is64BitOperatingSystem),
        ('powerShellVersion={0}' -f $PSVersionTable.PSVersion.ToString()),
        ('powerShellEdition={0}' -f $PSVersionTable.PSEdition)
    )
    Set-Content -LiteralPath (Join-Path $script:RunDirectory 'environment.txt') -Value $lines -Encoding UTF8
}

function Save-GitStatusFile {
    $repo = Resolve-ConfiguredPath ([string]$script:Config.repository.path) $script:ConfigBase
    $status = Invoke-Git $repo @('status','--porcelain')
    $branch = Invoke-Git $repo @('branch','--show-current')
    $head = Invoke-Git $repo @('rev-parse','HEAD')
    $lines = @(
        ('timestamp={0}' -f (Get-IsoTimestamp)),
        ('branch={0}' -f $branch.Output.Trim()),
        ('head={0}' -f $head.Output.Trim()),
        '',
        'git status --porcelain:',
        $status.Output
    )
    Set-Content -LiteralPath (Join-Path $script:RunDirectory 'git_status.txt') -Value $lines -Encoding UTF8
}

function Get-FirstEventTime {
    param([string]$EventName)
    $match = @($script:Events | Where-Object { $_.event -eq $EventName } | Select-Object -First 1)
    if ($match.Count -eq 0) { return $null }
    return $match[0].timestamp
}

function Write-Manifest {
    $repoInfo = $script:RepositoryInfo
    if ($null -eq $repoInfo) {
        $repoInfo = [pscustomobject]@{
            path = $(if ($script:Config) { [string]$script:Config.repository.path } else { $null })
            remoteUrl = $null
            branch = $null
            commit = $null
            shortCommit = $null
            dirty = $null
        }
    }

    $loggerInfo = @()
    foreach ($record in $script:LoggerProcesses) {
        $pidValue = $null
        if ($record.Process) { $pidValue = $record.Process.Id }
        $loggerInfo += [ordered]@{
            name = $record.Name
            kind = $record.Kind
            pid = $pidValue
            forcedStop = [bool]$record.ForcedStop
            stopError = $record.StopError
        }
    }
    $appPid = $null
    if ($script:ApplicationProcess) { $appPid = $script:ApplicationProcess.Id }
    $appForced = $false
    $appStopError = $null
    if ($script:ApplicationStopInfo) {
        $appForced = [bool]$script:ApplicationStopInfo.ForcedStop
        $appStopError = $script:ApplicationStopInfo.StopError
    }
    $manifest = [ordered]@{
        schemaVersion = 1
        runId = $script:RunId
        startTime = $script:StartTime
        endTime = $script:EndTime
        result = $script:Result
        failureMessage = $script:FailureMessage
        repository = [ordered]@{
            path = $repoInfo.path
            remoteUrl = $repoInfo.remoteUrl
            branch = $repoInfo.branch
            commit = $repoInfo.commit
            shortCommit = $repoInfo.shortCommit
            dirty = $repoInfo.dirty
            remote = [string]$script:Config.repository.remote
            pullResult = 'ff-only'
        }
        harness = [ordered]@{
            scriptPath = $PSCommandPath
            scriptSha256 = $script:HarnessScriptSha256
        }
        application = [ordered]@{
            path = [string]$script:Config.application.path
            resolvedPath = $(if ($script:ApplicationFileInfo) { $script:ApplicationFileInfo.path } else { $null })
            sha256 = $(if ($script:ApplicationFileInfo) { $script:ApplicationFileInfo.sha256 } else { $null })
            length = $(if ($script:ApplicationFileInfo) { $script:ApplicationFileInfo.length } else { $null })
            mtimeUtc = $(if ($script:ApplicationFileInfo) { $script:ApplicationFileInfo.mtimeUtc } else { $null })
            startTiming = [string]$script:Config.application.startTiming
            pid = $appPid
            forcedStop = $appForced
            stopError = $appStopError
        }
        dongle = [ordered]@{
            instanceIdPattern = [string]$script:Config.dongle.instanceIdPattern
            pollIntervalMs = [int]$script:Config.dongle.pollIntervalMs
        }
        logging = [ordered]@{
            flushWaitMs = [int]$script:Config.logging.flushWaitMs
            processes = $loggerInfo
        }
        events = [ordered]@{
            applicationStarted = Get-FirstEventTime 'ApplicationStarted'
            dongleConnected = Get-FirstEventTime 'DongleConnected'
            trackerPairing1 = Get-FirstEventTime 'TrackerPairing1Completed'
            trackerPowerOff = Get-FirstEventTime 'TrackerPowerOffCompleted'
            trackerPairing2 = Get-FirstEventTime 'TrackerPairing2Completed'
            loggingStopped = Get-FirstEventTime 'LoggingStopped'
        }
        package = [ordered]@{
            requested = [bool]$script:Config.output.createZip
            status = $script:PackageStatus
            zip = $(if ($script:Config.output.createZip) { 'run.zip' } else { $null })
        }
    }
    $manifest | ConvertTo-Json -Depth 20 |
        Set-Content -LiteralPath (Join-Path $script:RunDirectory 'manifest.json') -Encoding UTF8
}

function Write-SubmitMarkdown {
    $repoInfo = $script:RepositoryInfo
    if ($null -eq $repoInfo) {
        $repoInfo = [pscustomobject]@{
            remoteUrl = $null
            branch = $null
            commit = $null
        }
    }

    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add('# Dynamic Analysis Run')
    $lines.Add('')
    $lines.Add(('Run ID: {0}' -f $script:RunId))
    $lines.Add(('Result: {0}' -f $script:Result))
    if ($script:FailureMessage) { $lines.Add(('Failure: {0}' -f $script:FailureMessage)) }
    $lines.Add('')
    $lines.Add('## Git')
    $lines.Add('')
    $lines.Add(('Branch: {0}' -f $repoInfo.branch))
    $lines.Add(('Commit: {0}' -f $repoInfo.commit))
    $lines.Add(('Remote: {0}' -f $repoInfo.remoteUrl))
    $lines.Add('')
    $lines.Add('## Test Sequence')
    $lines.Add('')
    $lines.Add('1. Logging started with the dongle absent.')
    $lines.Add(('2. Application start timing: {0}.' -f $script:Config.application.startTiming))
    $lines.Add('3. Dongle inserted and detected.')
    $lines.Add('4. First tracker pairing/operator checkpoint.')
    $lines.Add('5. Tracker power-off/operator checkpoint.')
    $lines.Add('6. Second pairing/reconnect operator checkpoint.')
    $lines.Add('7. Logging stopped and artifacts collected.')
    $lines.Add('')
    $lines.Add('## Important Events')
    $lines.Add('')
    foreach ($event in $script:Events) {
        if ($event.event -notlike 'StateChanged' -and $event.event -notlike 'RunStarted') {
            $lines.Add(('- {0} {1}' -f $event.timestamp, $event.event))
        }
    }
    $lines.Add('')
    $lines.Add('## Files')
    $lines.Add('')
    Get-ChildItem -LiteralPath $script:RunDirectory -File -ErrorAction SilentlyContinue |
        Sort-Object Name | ForEach-Object { $lines.Add(('- {0}' -f $_.Name)) }
    $lines.Add('')
    $lines.Add('## Analysis Points')
    $lines.Add('')
    foreach ($point in $script:Config.analysisPoints) { $lines.Add(('- {0}' -f $point)) }
    $lines.Add('')
    $lines.Add('## Notes')
    $lines.Add('')
    $lines.Add('- events.jsonl is the authoritative harness/operator timeline.')
    $lines.Add('- Align packet/capture timestamps against event timestamps.')
    $lines.Add('- Process start or command-write success is not proof of RF semantic success.')
    Set-Content -LiteralPath (Join-Path $script:RunDirectory 'SUBMIT.md') -Value $lines -Encoding UTF8
}

function Create-RunZip {
    if (-not $script:Config.output.createZip) {
        $script:PackageStatus = 'disabled'
        return
    }
    $parent = Split-Path -Parent $script:RunDirectory
    $temporaryZip = Join-Path $parent ($script:RunId + '.package.zip')
    $finalZip = Join-Path $script:RunDirectory 'run.zip'
    if (Test-Path -LiteralPath $temporaryZip) { Remove-Item -LiteralPath $temporaryZip -Force }
    if (Test-Path -LiteralPath $finalZip) { Remove-Item -LiteralPath $finalZip -Force }
    Compress-Archive -Path (Join-Path $script:RunDirectory '*') -DestinationPath $temporaryZip -CompressionLevel Optimal
    Move-Item -LiteralPath $temporaryZip -Destination $finalZip
    $script:PackageStatus = 'created'
}

function Collect-Artifacts {
    try { Merge-DeviceEvents } catch { Write-HarnessLog ('Failed to merge device events: ' + $_.Exception.Message) 'WARN' }
    try { Save-GitStatusFile } catch { Write-HarnessLog ('Failed git snapshot: ' + $_.Exception.Message) 'WARN' }
    try { Save-EnvironmentInfo } catch { Write-HarnessLog ('Failed environment snapshot: ' + $_.Exception.Message) 'WARN' }
    try { Save-DeviceSnapshot (Join-Path $script:RunDirectory 'device_after.txt') } catch { Write-HarnessLog ('Failed device snapshot: ' + $_.Exception.Message) 'WARN' }
}

function Initialize-Harness {
    if (-not (Test-Path -LiteralPath $ConfigPath)) { throw "Config file not found: $ConfigPath" }
    $resolvedConfig = (Resolve-Path -LiteralPath $ConfigPath).Path
    $script:ConfigBase = Split-Path -Parent $resolvedConfig
    $script:Config = Get-Content -LiteralPath $resolvedConfig -Raw | ConvertFrom-Json
    $script:RunsDirectory = Resolve-ConfiguredPath ([string]$script:Config.output.runsDirectory) $script:ConfigBase
    New-Item -ItemType Directory -Path $script:RunsDirectory -Force | Out-Null
    $script:RunId = New-RunId $script:RunsDirectory
    $script:RunDirectory = Join-Path $script:RunsDirectory $script:RunId
    New-Item -ItemType Directory -Path $script:RunDirectory -Force | Out-Null
    $script:EventsPath = Join-Path $script:RunDirectory 'events.jsonl'
    $script:HarnessLogPath = Join-Path $script:RunDirectory 'harness.log'
    New-Item -ItemType File -Path $script:EventsPath -Force | Out-Null
    New-Item -ItemType File -Path $script:HarnessLogPath -Force | Out-Null
    $script:StartTime = Get-IsoTimestamp
    $script:HarnessScriptSha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash
    Add-HarnessEvent -Event 'RunStarted' -Source 'harness' -Details @{ runId = $script:RunId; harnessSha256 = $script:HarnessScriptSha256 }
    Write-HarnessLog ('Run directory: ' + $script:RunDirectory)
}

function Run-StateMachine {
    while ($script:State -ne 'DONE') {
        try {
            switch ($script:State) {
                'INIT' {
                    Initialize-Harness
                    Set-HarnessState 'PRECHECK'
                }
                'PRECHECK' {
                    $repo = Resolve-ConfiguredPath ([string]$script:Config.repository.path) $script:ConfigBase
                    if (-not (Test-Path -LiteralPath (Join-Path $repo '.git'))) { throw "Not a Git worktree: $repo" }
                    $pre = Get-RepositorySnapshot $repo
                    $script:RepositoryInfo = $pre
                    if ($pre.dirty) { throw 'Repository has uncommitted changes. No automatic stash/reset/clean is permitted.' }
                    if ($pre.branch -ne [string]$script:Config.repository.branch) {
                        throw ("Branch mismatch. Expected '{0}', actual '{1}'. No automatic branch switch is permitted." -f $script:Config.repository.branch, $pre.branch)
                    }
                    Assert-NoResidualProcesses
                    Save-DeviceSnapshot (Join-Path $script:RunDirectory 'device_before.txt')
                    Wait-ForDongleAbsent
                    Set-HarnessState 'UPDATE'
                }
                'UPDATE' {
                    $repo = Resolve-ConfiguredPath ([string]$script:Config.repository.path) $script:ConfigBase
                    [void](Invoke-Git $repo @('fetch',[string]$script:Config.repository.remote) -ThrowOnFailure)
                    [void](Invoke-Git $repo @('pull','--ff-only',[string]$script:Config.repository.remote,[string]$script:Config.repository.branch) -ThrowOnFailure)
                    $script:RepositoryInfo = Get-RepositorySnapshot $repo
                    if ($script:RepositoryInfo.dirty) { throw 'Repository became dirty after update.' }
                    Add-HarnessEvent -Event 'RepositoryUpdated' -Source 'git' -Details @{ branch = $script:RepositoryInfo.branch; commit = $script:RepositoryInfo.commit; result = 'ff-only' }
                    Set-HarnessState 'START_LOGGING'
                }
                'START_LOGGING' {
                    Start-ExternalLoggers
                    $script:LoggingActivated = $true
                    Add-HarnessEvent -Event 'LoggingStarted' -Source 'harness' -Details @{}
                    Set-HarnessState 'START_APPLICATION'
                }
                'START_APPLICATION' {
                    if ([string]$script:Config.application.startTiming -eq 'beforeDongle') {
                        Start-TargetApplication
                    } else {
                        Add-HarnessEvent -Event 'ApplicationStartDeferred' -Source 'harness' -Details @{ message = 'afterDongle' }
                    }
                    Set-HarnessState 'WAIT_DONGLE'
                }
                'WAIT_DONGLE' {
                    Wait-ForDonglePresent
                    Start-DongleWatcher
                    if ([string]$script:Config.application.startTiming -eq 'afterDongle') { Start-TargetApplication }
                    Set-HarnessState 'WAIT_PAIR_1'
                }
                'WAIT_PAIR_1' {
                    $op = Wait-Operator 'Trackerをペアリングしてください。' 'TrackerPairing1Completed'
                    if ($op -eq 'aborted') { $script:Result = 'aborted'; $script:FailureMessage = 'Operator aborted at first pairing.'; Set-HarnessState 'COLLECT_PARTIAL' }
                    elseif ($op -eq 'failed') { $script:Result = 'failed'; $script:FailureMessage = 'First pairing failed.'; Set-HarnessState 'ERROR' }
                    else { Set-HarnessState 'WAIT_POWER_OFF' }
                }
                'WAIT_POWER_OFF' {
                    $op = Wait-Operator 'Trackerの電源を切ってください。' 'TrackerPowerOffCompleted'
                    if ($op -eq 'aborted') { $script:Result = 'aborted'; $script:FailureMessage = 'Operator aborted at power-off.'; Set-HarnessState 'COLLECT_PARTIAL' }
                    elseif ($op -eq 'failed') { $script:Result = 'failed'; $script:FailureMessage = 'Tracker power-off step failed.'; Set-HarnessState 'ERROR' }
                    else { Set-HarnessState 'WAIT_PAIR_2' }
                }
                'WAIT_PAIR_2' {
                    $op = Wait-Operator 'Trackerを再度ペアリングしてください。' 'TrackerPairing2Completed'
                    if ($op -eq 'aborted') { $script:Result = 'aborted'; $script:FailureMessage = 'Operator aborted at second pairing.'; Set-HarnessState 'COLLECT_PARTIAL' }
                    elseif ($op -eq 'failed') { $script:Result = 'failed'; $script:FailureMessage = 'Second pairing failed.'; Set-HarnessState 'ERROR' }
                    else { Set-HarnessState 'STOP_LOGGING' }
                }
                'STOP_LOGGING' {
                    Stop-LoggingAndApplication
                    if ($script:Result -eq 'running') { $script:Result = 'completed' }
                    Set-HarnessState 'COLLECT'
                }
                'COLLECT' {
                    Collect-Artifacts
                    Set-HarnessState 'PACKAGE'
                }
                'ERROR' {
                    Add-HarnessEvent -Event 'RunError' -Source 'harness' -Details @{ message = $script:FailureMessage; result = $script:Result }
                    Set-HarnessState 'COLLECT_PARTIAL'
                }
                'COLLECT_PARTIAL' {
                    try { Stop-LoggingAndApplication } catch { Write-HarnessLog ('Cleanup failure: ' + $_.Exception.Message) 'WARN' }
                    Collect-Artifacts
                    if ($script:Result -eq 'running') { $script:Result = 'partial' }
                    Set-HarnessState 'PACKAGE'
                }
                'PACKAGE' {
                    if ($script:Config.output.createZip) {
                        $script:PackageStatus = 'created'
                    } else {
                        $script:PackageStatus = 'disabled'
                    }
                    $script:EndTime = Get-IsoTimestamp
                    Write-Manifest
                    Write-SubmitMarkdown
                    try {
                        Create-RunZip
                    } catch {
                        $script:PackageStatus = 'failed'
                        if ($script:Result -eq 'completed') { $script:Result = 'partial' }
                        Write-HarnessLog ('ZIP generation failed: ' + $_.Exception.Message) 'WARN'
                    }
                    $script:EndTime = Get-IsoTimestamp
                    Write-Manifest
                    Write-SubmitMarkdown
                    Set-HarnessState 'DONE'
                }
                default { throw "Unknown state: $($script:State)" }
            }
        } catch {
            $script:FailureMessage = $_.Exception.Message
            if ($script:Result -eq 'running') { $script:Result = 'failed' }
            if (-not $script:EventsPath) { throw }
            Write-HarnessLog $script:FailureMessage 'ERROR'
            Add-HarnessEvent -Event 'Exception' -Source 'harness' -Details @{ message = $script:FailureMessage; result = $script:Result }
            if ($script:State -eq 'PACKAGE') {
                $script:EndTime = Get-IsoTimestamp
                try { Write-Manifest } catch {}
                try { Write-SubmitMarkdown } catch {}
                Set-HarnessState 'DONE'
            } elseif ($script:State -eq 'ERROR' -or $script:State -eq 'COLLECT_PARTIAL') {
                Set-HarnessState 'PACKAGE'
            } else {
                Set-HarnessState 'ERROR'
            }
        }
    }
}

Run-StateMachine

Write-Host ''
Write-Host ('Run ID: {0}' -f $script:RunId)
Write-Host ('Result: {0}' -f $script:Result)
Write-Host ('Artifacts: {0}' -f $script:RunDirectory)

if ($script:Result -eq 'completed') { exit 0 }
if ($script:Result -eq 'aborted') { exit 2 }
exit 1
