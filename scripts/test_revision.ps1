[CmdletBinding()]
param(
    [ValidateSet('Quick', 'Full')]
    [string]$Mode = 'Quick',
    [string]$ResultsDir = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$StartedUtc = (Get-Date).ToUniversalTime()
$RunId = $StartedUtc.ToString('yyyyMMdd-HHmmss')
if ([string]::IsNullOrWhiteSpace($ResultsDir)) {
    $ResultsDir = Join-Path $RepoRoot ("build/revision-test-results/{0}-{1}" -f $RunId, $Mode.ToLowerInvariant())
}
New-Item -ItemType Directory -Force -Path $ResultsDir | Out-Null

$StepResults = [System.Collections.Generic.List[object]]::new()
$FailedStep = $null

function Invoke-RevisionStep {
    param([string]$Name, [string]$FilePath, [string[]]$Arguments)

    $logPath = Join-Path $ResultsDir ((($Name -replace '[^A-Za-z0-9_.-]', '_')) + '.log')
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    $global:LASTEXITCODE = 0
    $exitCode = 0
    $previousErrorActionPreference = $ErrorActionPreference
    Push-Location $RepoRoot
    try {
        $ErrorActionPreference = 'Continue'
        & $FilePath @Arguments *> $logPath
        if ($null -ne $LASTEXITCODE) { $exitCode = [int]$LASTEXITCODE }
    }
    catch {
        ($_ | Out-String) | Add-Content -Path $logPath
        $exitCode = 1
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
        Pop-Location
        $timer.Stop()
    }

    $StepResults.Add([pscustomobject]@{
        name = $Name
        result = $(if ($exitCode -eq 0) { 'PASS' } else { 'FAIL' })
        exit_code = $exitCode
        duration_ms = $timer.ElapsedMilliseconds
        log = $logPath
    })
    return ($exitCode -eq 0)
}

$BuildDir = Join-Path $RepoRoot ("build/revision-{0}" -f $Mode.ToLowerInvariant())
$Steps = @(
    @{ Name='release-tooling'; File='python'; Args=@('scripts/test_release_v2.py') },
    @{ Name='release-tooling-optimized'; File='python'; Args=@('-O','scripts/test_release_v2.py') },
    @{ Name='protocol-v2-kit'; File='python'; Args=@('scripts/verify_protocol_v2.py') },
    @{ Name='protocol-v1-historical-kit'; File='python'; Args=@('scripts/import_protocol_kit.py','--verify') },
    @{ Name='cmake-configure'; File='cmake'; Args=@('-S','.','-B',$BuildDir,'-A','x64','-DBUILD_TESTING=ON','-DVUT_BUILD_OBSERVATION=ON') },
    @{ Name='cmake-build'; File='cmake'; Args=@('--build',$BuildDir,'--config','Release') }
)

if ($Mode -eq 'Quick') {
    $Steps += @{ Name='ctest-quick'; File='ctest'; Args=@('--test-dir',$BuildDir,'-C','Release','--output-on-failure','-R','protocol_v2_integrity|protocol$|observation|protocol_kit_integrity|runtime_policy|service_session_policy|backend_pairing|launcher_process_guard') }
}
else {
    $Steps += @{ Name='ctest-full'; File='ctest'; Args=@('--test-dir',$BuildDir,'-C','Release','--output-on-failure') }
}

foreach ($step in $Steps) {
    if (-not (Invoke-RevisionStep -Name $step.Name -FilePath $step.File -Arguments $step.Args)) {
        $FailedStep = $step.Name
        break
    }
}

$head = ''
try { $head = ((& git -C $RepoRoot rev-parse HEAD 2>$null) | Out-String).Trim() } catch { }
$result = if ($null -eq $FailedStep) { 'PASS' } else { 'FAIL' }
$summary = [ordered]@{
    repo = 'orzkwsk/ViveUltimateTrackerBridge'
    head = $head
    mode = $Mode
    result = $result
    failed_step = $FailedStep
    started_utc = $StartedUtc.ToString('o')
    ended_utc = (Get-Date).ToUniversalTime().ToString('o')
    hardware_validation = 'NOT RUN'
    vive_hub_bypass_hardware = 'NOT RUN'
    rotation_only_hardware_semantics = 'NOT RUN'
    release_packaging = 'NOT RUN'
    steps = $StepResults
}
$summaryPath = Join-Path $ResultsDir 'summary.json'
$summary | ConvertTo-Json -Depth 6 | Set-Content -Path $summaryPath -Encoding UTF8
Write-Host ("RESULT={0} repo=ViveUltimateTrackerBridge mode={1} failed_step={2} summary={3}" -f $result, $Mode, $FailedStep, $summaryPath)
if ($result -eq 'FAIL') { exit 1 }
