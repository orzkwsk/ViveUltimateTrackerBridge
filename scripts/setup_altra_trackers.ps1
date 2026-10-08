[CmdletBinding()]
param([string]$CMake = 'cmake', [string]$AssignmentsPath = '')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $RepoRoot 'build/setup'
if ([string]::IsNullOrWhiteSpace($AssignmentsPath)) {
    $AssignmentsPath = Join-Path $env:LOCALAPPDATA 'MonakaVR/ViveUltimateTrackerBridge/assignments.json'
}
Write-Host 'Setup Altra Trackers: discovery and motion Identify are available.'
Write-Host 'Stable identity acquisition and upper consumer role application are not integrated.'
Write-Host 'Stop vut_service and VIVE Hub before using this exclusive dongle setup tool.'
& $CMake -S $RepoRoot -B $BuildDir -A x64 -DBUILD_TESTING=OFF
if ($LASTEXITCODE -ne 0) { throw 'Setup configure failed.' }
& $CMake --build $BuildDir --config Release --target vut_setup vut_pair
if ($LASTEXITCODE -ne 0) { throw 'Setup build failed.' }
$FullAssignmentsPath = [System.IO.Path]::GetFullPath($AssignmentsPath)
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $FullAssignmentsPath) | Out-Null
Write-Host "Assignment sidecar: $FullAssignmentsPath"
Write-Host ("If physical pairing is needed, quit setup and run: {0}" -f (Join-Path $BuildDir 'Release/vut_pair.exe'))
& (Join-Path $BuildDir 'Release/vut_setup.exe') $FullAssignmentsPath
exit $LASTEXITCODE
