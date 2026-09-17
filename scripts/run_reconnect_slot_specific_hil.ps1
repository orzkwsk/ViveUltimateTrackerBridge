[CmdletBinding()]
param(
    [switch]$RunProbe
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$RequiredCommit = '1c8f8a599dc92062bf44e3b79503f921d64636dd'
$BuildDir = Join-Path $RepoRoot 'build/reconnect-slot-specific'

Push-Location $RepoRoot
try {
    $head = ((& git rev-parse HEAD) | Out-String).Trim()
    $branch = ((& git branch --show-current) | Out-String).Trim()

    & git merge-base --is-ancestor $RequiredCommit HEAD
    if ($LASTEXITCODE -ne 0) {
        throw "Current HEAD $head does not contain required slot-specific RestartRf commit $RequiredCommit. Fetch/pull experiment/vive-auto-reconnect before testing."
    }

    $cmake = Get-Command cmake -ErrorAction SilentlyContinue
    if ($null -eq $cmake) {
        $cmakeDir = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
        if (-not (Test-Path (Join-Path $cmakeDir 'cmake.exe'))) {
            throw 'cmake was not found in PATH or the expected Visual Studio 18 Insiders location.'
        }
        $env:Path = "$cmakeDir;$env:Path"
    }

    Write-Host "repo=$RepoRoot"
    Write-Host "branch=$branch"
    Write-Host "HEAD=$head"
    Write-Host "required_slot_specific_commit=$RequiredCommit"
    Write-Host 'expected_cold_start_restart_rf=02 01 00 01 01 01 01'
    Write-Host 'expected_runtime_slot0_restart_rf=02 01 01 00 00 00 00'
    Write-Host 'expected_runtime_slot2_restart_rf=02 01 00 00 01 00 00'
    Write-Host "build_dir=$BuildDir"

    # --fresh prevents a previously configured/reused revision-quick tree from
    # accidentally supplying an older vut_probe binary for this HIL.
    & cmake --fresh -S $RepoRoot -B $BuildDir -A x64 -DBUILD_TESTING=ON -DVUT_BUILD_OBSERVATION=OFF
    if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }

    & cmake --build $BuildDir --config Release --target vut_probe vut_protocol_tests
    if ($LASTEXITCODE -ne 0) { throw 'cmake build failed' }

    $protocolTests = Join-Path $BuildDir 'Release\vut_protocol_tests.exe'
    if (-not (Test-Path $protocolTests)) { throw "protocol test executable not found: $protocolTests" }
    & $protocolTests
    if ($LASTEXITCODE -ne 0) { throw 'vut_protocol_tests failed' }

    $probe = Join-Path $BuildDir 'Release\vut_probe.exe'
    if (-not (Test-Path $probe)) { throw "probe executable not found: $probe" }

    Write-Host "HIL_PROBE=$probe"
    Write-Host 'Before runtime reconnect judgment, verify USBPcap shows slot0 payload 02 01 01 00 00 00 00 after DeviceUnpaired.'

    if ($RunProbe) {
        & $probe
        exit $LASTEXITCODE
    }
}
finally {
    Pop-Location
}
