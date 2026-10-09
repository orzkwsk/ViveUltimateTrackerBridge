param([Parameter(Mandatory=$true)][string]$Executable)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$binary = (Resolve-Path -LiteralPath $Executable).Path
$embedded = & $binary --version | ConvertFrom-Json
if ($LASTEXITCODE -ne 0) { throw 'Cannot read embedded build provenance' }
$head = (& git -C $repo rev-parse HEAD).Trim()
$status = & git -C $repo status --porcelain --untracked-files=normal
$exact = $embedded.git_commit -eq $head -and $embedded.dirty_at_build -eq $false -and -not $status
$embedded.binary_sha256 = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant()
[ordered]@{
    build = $embedded
    source_head = $head
    source_clean = -not [bool]$status
    mapping_confidence = $(if ($exact) { 'EXACT' } else { 'UNKNOWN' })
    binary_hash_binding = 'Hash of the exact executable queried for its embedded metadata'
} | ConvertTo-Json -Depth 6
if (-not $exact) { exit 2 }
