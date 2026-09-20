param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$BuildConfig = "Release",
    [string]$ConfigPath = "",
    [string]$JournalPath = "",
    [ValidateRange(1, 65535)]
    [int]$Port = 29810,
    [switch]$NoBuild,
    [switch]$Diagnostics
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'vive_launcher_process.ps1')

function Resolve-RepoPath {
    param([string]$Path)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
}

function Find-CMake {
    $fromPath = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($fromPath) { return $fromPath.Source }
    $vsCMake = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (Test-Path $vsCMake) { return $vsCMake }
    throw "cmake.exe was not found. Install CMake or Visual Studio C++/CMake support."
}

if (-not $ConfigPath) { $ConfigPath = "config\vut-service.conf" }
$ConfigPath = Resolve-RepoPath $ConfigPath

if (-not $JournalPath) {
    if (-not $env:LOCALAPPDATA) { throw "LOCALAPPDATA is not available. Pass -JournalPath explicitly." }
    $JournalPath = Join-Path $env:LOCALAPPDATA "MonakaVR\ViveUltimateTrackerBridge\vut-service.revision.log"
}
$JournalPath = [System.IO.Path]::GetFullPath($JournalPath)

if (-not (Test-Path $ConfigPath -PathType Leaf)) {
    $example = Join-Path $RepoRoot "config\vut-service.example.conf"
    Write-Host ""
    Write-Host "VIVE service configuration is missing:" -ForegroundColor Yellow
    Write-Host "  $ConfigPath"
    Write-Host ""
    Write-Host "Create it from the example and edit source/map values:"
    Write-Host "  Copy-Item `"$example`" `"$ConfigPath`""
    Write-Host ""
    Write-Host "The map key is currently the full RF address accepted by vut_service."
    Write-Host "Do not treat that address as a persistent hardware identity; update/restart if it changes."
    throw "vut_service configuration is not prepared."
}

$configText = Get-Content $ConfigPath -Raw
if ($configText -match "vive-installation-example" -or
    $configText -match "23:32:42:b7:82:d3" -or
    $configText -match "24:32:42:b7:82:d3") {
    throw "Config still contains example placeholder values. Edit $ConfigPath before validation."
}

$journalDir = Split-Path -Parent $JournalPath
if ($journalDir -and -not (Test-Path $journalDir)) {
    New-Item -ItemType Directory -Path $journalDir -Force | Out-Null
}

$buildDir = Join-Path $RepoRoot "build\runtime"
$exe = Join-Path $buildDir "$BuildConfig\vut_service.exe"
$running = @(Get-CimInstance Win32_Process -Filter "Name='vut_service.exe'" -ErrorAction Stop)
$matching = Get-ViveMatchingProcess -Processes $running -Executable $exe -ConfigPath $ConfigPath -JournalPath $JournalPath -Port $Port -Diagnostics ([bool]$Diagnostics)
if ($null -ne $matching) {
    Write-Host "Matching vut_service process exists (PID $($matching.ProcessId)); binary path, config, journal and options match."
    Write-Host 'This is process identity confirmation, not a hardware/stream health check. No rebuild was performed.'
    exit 0
}

if (-not $NoBuild) {
    $cmake = Find-CMake
    Write-Host "Configuring VIVE backend runtime..."
    & $cmake -S $RepoRoot -B $buildDir -A x64 -DBUILD_TESTING=OFF -DVUT_BUILD_OBSERVATION=ON
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

    Write-Host "Building vut_service ($BuildConfig)..."
    & $cmake --build $buildDir --config $BuildConfig --target vut_service
    if ($LASTEXITCODE -ne 0) { throw "vut_service build failed." }
}

if (-not (Test-Path $exe -PathType Leaf)) {
    throw "vut_service.exe was not found: $exe. Run without -NoBuild first."
}

Write-Host ""
Write-Host "Starting VIVE Ultimate Tracker backend service..."
Write-Host "  exe:     $exe"
Write-Host "  config:  $ConfigPath"
Write-Host "  journal: $JournalPath"
Write-Host "  target:  127.0.0.1:$Port"
Write-Host ""
Write-Host "Close VIVE Hub / VIVE Streaming Hub before using the official dongle."
Write-Host "Ctrl+C stops the service."
Write-Host ""

$serviceArguments = @($ConfigPath, $JournalPath, $Port)
if ($Diagnostics) { $serviceArguments += '--diagnostics' }
& $exe @serviceArguments
exit $LASTEXITCODE
