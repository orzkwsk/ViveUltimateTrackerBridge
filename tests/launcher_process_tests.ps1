$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot '../scripts/vive_launcher_process.ps1')

function Require($Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
$executable = 'C:\Fixture With Spaces\vut_service.exe'
$config = 'C:\Fixture With Spaces\service.conf'
$journal = 'C:\Fixture With Spaces\journal.log'
$command = '"{0}" "{1}" "{2}"' -f $executable, $config, $journal
function Candidate([string]$Path, [string]$Arguments) {
    return [pscustomobject]@{ProcessId=123;ExecutablePath=$Path;CommandLine=$Arguments}
}
function Match([object[]]$Processes, [bool]$Diagnostic=$false) {
    return Get-ViveMatchingProcess -Processes $Processes -Executable $executable -ConfigPath $config -JournalPath $journal -Port 29810 -Diagnostics $Diagnostic
}
Require ($null -eq (Match @())) 'Empty process list must allow a new launch'
Require ((Match @(Candidate $executable $command)).ProcessId -eq 123) 'Default port and quoted paths'
Require ((Match @(Candidate $executable ($command + ' 29810'))).ProcessId -eq 123) 'Explicit port'
Require ((Match @(Candidate $executable ($command + ' --diagnostics 29810')) $true).ProcessId -eq 123) 'Diagnostic options'
Require ((Match @(Candidate $executable.ToUpperInvariant() $command)).ProcessId -eq 123) 'Windows path casing'
Require (-not (Test-ViveSameAbsolutePath 'C:relative.conf' $config)) 'Drive-relative path is not absolute'
Require (-not (Test-ViveSameAbsolutePath '\Fixture With Spaces\service.conf' $config)) 'Root-relative path is not absolute'
foreach ($candidate in @(
    (Candidate 'C:\Old Build\vut_service.exe' $command),
    (Candidate $executable ($command.Replace($executable, 'C:\Old Build\vut_service.exe'))),
    (Candidate $executable ($command.Replace($config, 'C:\Other\service.conf'))),
    (Candidate $executable ($command.Replace($journal, 'C:\Other\journal.log'))),
    (Candidate $executable ($command.Replace($config, 'relative.conf'))),
    (Candidate $executable ($command + ' 29811')),
    (Candidate $executable ($command + ' --diagnostics')),
    (Candidate $executable ($command + ' --diagnostic-slam-query')),
    (Candidate $executable ($command + ' 29810 29810')),
    (Candidate $executable ($command + ' --diagnostics --diagnostics')),
    (Candidate $executable ($command + ' --DIAGNOSTICS')),
    (Candidate '' $command), (Candidate $executable ''),
    (Candidate $executable ($command + ' 999999999999999999999999'))
)) {
    $rejected = $false
    try { Match @($candidate) | Out-Null } catch { $rejected = $true }
    Require $rejected ('Incorrect/uninspectable process was accepted: ' + $candidate.CommandLine)
}
$rejected = $false
try { Match @(Candidate $executable ($command + ' --DIAGNOSTICS')) $true | Out-Null } catch { $rejected=$true }
Require $rejected 'Option matching must have the same case sensitivity as vut_service'
$rejected = $false
try { Match @((Candidate $executable $command),(Candidate $executable $command)) | Out-Null } catch { $rejected=$true }
Require $rejected 'Multiple matching processes must not be reported as success'
Write-Host 'PASS launcher process matching: binary/config/journal/options, mismatches and unknown identity; no service or HID opened'
