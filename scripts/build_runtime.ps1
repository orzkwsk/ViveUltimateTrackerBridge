param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$BuildConfig = 'Release',
    [switch]$WithTests
)
$ErrorActionPreference = 'Stop'
if ($args.Count) { throw "Unknown arguments: $($args -join ' ')" }
$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildDirectory = Join-Path $RepoRoot 'Build\runtime'

function Find-CMake {
    if ($env:MONAKA_CMAKE) {
        if (Test-Path -LiteralPath $env:MONAKA_CMAKE -PathType Leaf) { return $env:MONAKA_CMAKE }
        throw 'MONAKA_CMAKE must name an existing CMake executable.'
    }
    $tool = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($tool) { return $tool.Source }
    foreach ($programRoot in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        if (-not $programRoot) { continue }
        $standalone = Join-Path $programRoot 'CMake\bin\cmake.exe'
        if (Test-Path -LiteralPath $standalone -PathType Leaf) { return $standalone }
        $pattern = Join-Path $programRoot 'Microsoft Visual Studio\*\*\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        $candidate = Get-ChildItem -Path $pattern -File -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1
        if ($candidate) { return $candidate.FullName }
    }
    throw 'CMake was not found. Install CMake/Visual Studio C++ support or set MONAKA_CMAKE.'
}

$cmake = Find-CMake
$testSetting = if ($WithTests) { 'ON' } else { 'OFF' }
& $cmake -S $RepoRoot -B $BuildDirectory -A x64 "-DBUILD_TESTING=$testSetting" -DVUT_BUILD_OBSERVATION=ON
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmake --build $BuildDirectory --config $BuildConfig --parallel 2
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Built $BuildDirectory\$BuildConfig. No service or hardware was started."
exit 0
