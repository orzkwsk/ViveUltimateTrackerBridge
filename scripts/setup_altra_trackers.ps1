[CmdletBinding()]
param([string]$CMake = 'cmake')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $RepoRoot 'Build/setup'
Write-Host 'Setup Altra Trackers: same-session Pairing, discovery and provisional motion Identify.'
Write-Host 'Stable identity acquisition and upper consumer role application are not integrated.'
Write-Host 'Stop vut_service and VIVE Hub before using this exclusive dongle setup tool.'
& $CMake -S $RepoRoot -B $BuildDir -A x64 -DBUILD_TESTING=OFF -DVUT_BUILD_ASSIGNMENT_LAB=OFF
if ($LASTEXITCODE -ne 0) { throw 'Setup configure failed.' }
& $CMake --build $BuildDir --config Release --target vut_setup vut_pair
if ($LASTEXITCODE -ne 0) { throw 'Setup build failed.' }
Write-Host 'Use pair [all|0|1|2|3|4], pair-cancel and pair-status inside setup.'
Write-Host 'This exclusive direct-HID console is a bring-up path; service-owned live control is not implemented.'
& (Join-Path $BuildDir 'Release/vut_setup.exe')
exit $LASTEXITCODE
