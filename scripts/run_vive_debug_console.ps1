[CmdletBinding()]
param(
    [switch]$NoRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$ExpectedBranch = 'experiment/vive-debug-console'
$BuildDir = Join-Path $RepoRoot 'build/debug-console'

Push-Location $RepoRoot
try {
    $branch = ((& git branch --show-current) | Out-String).Trim()
    $head = ((& git rev-parse HEAD) | Out-String).Trim()

    if ($branch -ne $ExpectedBranch) {
        throw "Expected branch '$ExpectedBranch' but current branch is '$branch'."
    }

    $cmake = Get-Command cmake -ErrorAction SilentlyContinue
    if ($null -eq $cmake) {
        $cmakeDir = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
        if (-not (Test-Path (Join-Path $cmakeDir 'cmake.exe'))) {
            throw 'cmake was not found in PATH or the expected Visual Studio 18 Insiders location.'
        }
        $env:Path = "$cmakeDir;$env:Path"
    }

    $vsDevCmd = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat'
    if ($null -eq (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        if (-not (Test-Path $vsDevCmd)) {
            throw 'cl.exe not found and VsDevCmd.bat is not at the expected Visual Studio 18 Insiders location.'
        }

        cmd.exe /s /c "`"$vsDevCmd`" -arch=x64 -host_arch=x64 >nul && set" |
            ForEach-Object {
                if ($_ -match '^([^=]+)=(.*)$') {
                    Set-Item -Path "Env:$($matches[1])" -Value $matches[2]
                }
            }
    }

    Write-Host "repo=$RepoRoot"
    Write-Host "branch=$branch"
    Write-Host "HEAD=$head"
    Write-Host "build_dir=$BuildDir"

    & cmake --fresh -S $RepoRoot -B $BuildDir -A x64 -DBUILD_TESTING=ON -DVUT_BUILD_OBSERVATION=OFF
    if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }

    & cmake --build $BuildDir --config Release --target vut_debug_console vut_protocol_tests
    if ($LASTEXITCODE -ne 0) { throw 'cmake build failed' }

    $tests = Join-Path $BuildDir 'Release\vut_protocol_tests.exe'
    if (-not (Test-Path $tests)) { throw "protocol tests not found: $tests" }

    & $tests
    if ($LASTEXITCODE -ne 0) { throw 'vut_protocol_tests failed' }

    $console = Join-Path $BuildDir 'Release\vut_debug_console.exe'
    if (-not (Test-Path $console)) { throw "DebugConsole not found: $console" }

    $file = Get-Item $console
    $hash = Get-FileHash -Algorithm SHA256 $console
    Write-Host "DEBUG_CONSOLE=$($file.FullName)"
    Write-Host "DEBUG_CONSOLE_SHA256=$($hash.Hash)"
    Write-Host "DEBUG_CONSOLE_LENGTH=$($file.Length)"
    Write-Host "DEBUG_CONSOLE_MTIME_UTC=$($file.LastWriteTimeUtc.ToString('o'))"

    if (-not $NoRun) {
        & $console
        exit $LASTEXITCODE
    }
}
finally {
    Pop-Location
}
