[CmdletBinding()]
param([string]$ConfigPath=(Join-Path $PSScriptRoot 'config.json'))
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'

$S=[ordered]@{
 State='INIT';RunId=$null;RunDir=$null;HarnessLog=$null;EventsPath=$null;ManifestPath=$null
 Result='running';Failure=$null;StartTime=$null;EndTime=$null;Repo=$null;App=$null
 Git=[ordered]@{};AppInfo=[ordered]@{};Loggers=@();LoggerInfo=@();Events=[ordered]@{};Cleanup=@()
}

function NowIso { [DateTimeOffset]::Now.ToString('yyyy-MM-ddTHH:mm:ss.fffzzz') }
function Log([string]$m,[string]$lvl='INFO'){
 $line="$(NowIso) [$lvl] [$($S.State)] $m";Write-Host $line
 if($S.HarnessLog){Add-Content -LiteralPath $S.HarnessLog -Value $line -Encoding UTF8}
}
function Event([string]$name,[string]$source,[string]$message='',[string]$result='',[Nullable[int]]$pid=$null,[string]$device=''){
 if(-not$S.EventsPath){return}
 $o=[ordered]@{timestamp=NowIso;event=$name;source=$source}
 if($message){$o.message=$message};if($result){$o.result=$result};if($null-ne$pid){$o.pid=$pid.Value};if($device){$o.device=$device}
 Add-Content -LiteralPath $S.EventsPath -Value ($o|ConvertTo-Json -Compress -Depth 6) -Encoding UTF8
 if(-not$S.Events.Contains($name)){$S.Events[$name]=$o.timestamp}
}
function State([string]$n){$p=$S.State;$S.State=$n;Log "State: $p -> $n";Event 'StateChanged' 'harness' "$p -> $n"}
function RPath([string]$p,[string]$base){if([IO.Path]::IsPathRooted($p)){[IO.Path]::GetFullPath($p)}else{[IO.Path]::GetFullPath((Join-Path $base $p))}}
function Exec([string]$exe,[string[]]$args,[string]$wd){
 $old=Get-Location;try{if($wd){Set-Location -LiteralPath $wd};Log "Exec: $exe $($args -join ' ')";$o=& $exe @args 2>&1;[pscustomobject]@{Exit=$LASTEXITCODE;Out=@($o|%{"$_"})}}finally{Set-Location $old}
}
function Pnp([string]$pat){
 if($null-eq(Get-Command Get-PnpDevice -ErrorAction SilentlyContinue)){throw 'Get-PnpDevice unavailable'}
 @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue|?{$_.InstanceId -like $pat})
}
function Dongle([string]$pat){(Pnp $pat).Count-gt0}
function Snapshot([string]$pat,[string]$file){
 $d=@(Pnp $pat|select Status,Class,FriendlyName,InstanceId,Problem,Present)
 if($d.Count){$d|fl *|Out-String|Set-Content -LiteralPath $file -Encoding UTF8}else{'No matching present device.'|Set-Content -LiteralPath $file -Encoding UTF8}
}
function WaitDongle([string]$pat,[bool]$want,[int]$poll,[int]$timeout){
 $sw=[Diagnostics.Stopwatch]::StartNew();$last=$null
 while($true){$v=Dongle $pat;if($v-ne$last){Log "Dongle present=$v";$last=$v};if($v-eq$want){return $true};if($timeout-gt0-and$sw.ElapsedMilliseconds-ge$timeout){return $false};Start-Sleep -Milliseconds $poll}
}
function Operator([string]$prompt,[bool]$skip=$false){
 while($true){Write-Host '';Write-Host $prompt;if($skip){Write-Host 'Enter=完了 / F=Failed / S=Skip / Q=Abort'}else{Write-Host 'Enter=完了 / F=Failed / Q=Abort'}
  $x=(Read-Host).Trim().ToUpperInvariant();if($x-eq''){return'completed'};if($x-eq'F'){return'failed'};if($x-eq'Q'){return'aborted'};if($skip-and$x-eq'S'){return'skipped'}
 }
}
function StartProc([string]$exe,[string]$arg,[string]$wd,[string]$out,[string]$err){
 if(-not(Test-Path -LiteralPath $exe -PathType Leaf)){throw "Executable not found: $exe"}
 $p=@{FilePath=$exe;WorkingDirectory=$wd;PassThru=$true;RedirectStandardOutput=$out;RedirectStandardError=$err};if($arg){$p.ArgumentList=$arg};Start-Process @p
}
function StopProc($p,[string]$name,[int]$wait,[bool]$force){
 try{if($p.HasExited){return'already-exited'};$closed=$false;try{$closed=$p.CloseMainWindow()}catch{};if($closed-and$p.WaitForExit($wait)){return'graceful'}
  if($force){Stop-Process -Id $p.Id -Force;$p.WaitForExit([Math]::Max($wait,1000))|Out-Null;$S.Cleanup+=("$name force-stopped PID="+$p.Id);return'forced'}
  $S.Cleanup+=("$name left running PID="+$p.Id);return'left-running'
 }catch{$S.Cleanup+=("Stop failed $name: "+$_.Exception.Message);return'stop-failed'}
}
function RunId([string]$runs){
 New-Item -ItemType Directory -Path $runs -Force|Out-Null;$now=Get-Date;$d=$now.ToString('yyyyMMdd');$n=1
 $rx='^'+[regex]::Escape($d)+'_\d{6}_(\d{3})'
 Get-ChildItem $runs -Directory -ErrorAction SilentlyContinue|%{if($_.Name-match$rx){$v=[int]$matches[1];if($v-ge$n){$n=$v+1}}}
 $S.RunId='{0}_{1}_{2:D3}'-f$d,$now.ToString('HHmmss'),$n;$S.RunDir=Join-Path $runs ($S.RunId+'_pending');New-Item -ItemType Directory $S.RunDir|Out-Null
 $S.HarnessLog=Join-Path $S.RunDir 'harness.log';$S.EventsPath=Join-Path $S.RunDir 'events.jsonl';$S.ManifestPath=Join-Path $S.RunDir 'manifest.json';$S.StartTime=NowIso
 Log "Run initialized $($S.RunId)";Event 'RunStarted' 'harness' $S.RunId
}
function RenameRun([string]$short){
 if(-not$S.RunDir.EndsWith('_pending')){return};$parent=Split-Path -Parent $S.RunDir;$target=Join-Path $parent ($S.RunId+'_'+$short)
 Rename-Item $S.RunDir (Split-Path -Leaf $target);$S.RunDir=$target;$S.HarnessLog=Join-Path $target 'harness.log';$S.EventsPath=Join-Path $target 'events.jsonl';$S.ManifestPath=Join-Path $target 'manifest.json'
}
function GitInfo([string]$repo){
 $lines=@();foreach($a in @(@('status','--porcelain=v1','--branch'),@('remote','-v'),@('log','-1','--oneline','--decorate=short'))){$r=Exec git $a $repo;$lines+=">>> git "+($a-join' ');$lines+=$r.Out;$lines+=''}
 $lines|Set-Content (Join-Path $S.RunDir 'git_status.txt') -Encoding UTF8
}
function StartLoggers($cfg,[string]$base){
 foreach($l in @($cfg)){if($null-eq$l-or-not$l.enabled){continue};$exe=RPath ([string]$l.path) $base;$wd=if($l.workingDirectory){RPath ([string]$l.workingDirectory) $base}else{Split-Path -Parent $exe}
  $name=if($l.name){[string]$l.name}else{[IO.Path]::GetFileNameWithoutExtension($exe)};$p=StartProc $exe ([string]$l.arguments) $wd (Join-Path $S.RunDir "$name.stdout.log") (Join-Path $S.RunDir "$name.stderr.log")
  $S.Loggers+=[pscustomobject]@{Name=$name;P=$p;Force=[bool]$l.allowForceStop};$S.LoggerInfo+=@([ordered]@{name=$name;path=$exe;pid=$p.Id;startTime=NowIso;stopMode=$null});Log "Logger $name PID=$($p.Id)";Event 'LoggerStarted' 'harness' $name '' $p.Id
 }
}
function StopLoggers([int]$wait,[int]$flush){
 foreach($l in @($S.Loggers)){$mode=StopProc $l.P ('logger:'+$l.Name) $wait $l.Force;foreach($m in $S.LoggerInfo){if($m.name-eq$l.Name-and$m.pid-eq$l.P.Id){$m.stopMode=$mode;$m.endTime=NowIso}};Event 'LoggerStopped' 'harness' ($l.Name+':'+$mode)}
 if($flush-gt0){Start-Sleep -Milliseconds $flush}
}
function MergeApp{
 $dst=Join-Path $S.RunDir 'application.log';$all=@();foreach($f in @('application.stdout.log','application.stderr.log')){$p=Join-Path $S.RunDir $f;if(Test-Path $p){$all+="===== $f =====";$all+=Get-Content $p -ErrorAction SilentlyContinue}}
 if($all.Count){$all|Set-Content $dst -Encoding UTF8}
}
function Manifest($cfg){
 $S.EndTime=NowIso
 [ordered]@{schemaVersion=1;runId=$S.RunId;startTime=$S.StartTime;endTime=$S.EndTime;result=$S.Result;state=$S.State;failure=$S.Failure;repository=$S.Git;application=$S.AppInfo;loggers=@($S.LoggerInfo);events=$S.Events;cleanupNotes=@($S.Cleanup);environment=[ordered]@{computerName=$env:COMPUTERNAME;userName=$env:USERNAME;os=[Environment]::OSVersion.VersionString;powerShell=$PSVersionTable.PSVersion.ToString()};config=[ordered]@{dongleInstanceIdPattern=[string]$cfg.dongle.instanceIdPattern;pollIntervalMs=[int]$cfg.dongle.pollIntervalMs;createZip=[bool]$cfg.output.createZip}}|ConvertTo-Json -Depth 12|Set-Content $S.ManifestPath -Encoding UTF8
}
function Submit($cfg){
 $x=@('# Dynamic Analysis Run','',"Run ID: $($S.RunId)","Result: $($S.Result)",'','## Git','',"Branch: $($S.Git.branch)","Commit: $($S.Git.commit)",'','## Important Events','')
 foreach($k in $S.Events.Keys){$x+="- $($S.Events[$k]) - $k"};$x+='';$x+='## Files';$x+='';$x+=@(Get-ChildItem $S.RunDir -File|sort Name|%{'- '+$_.Name});$x+='';$x+='## Analysis Points';$x+='';$x+=@($cfg.analysisPoints|%{'- '+$_});$x|Set-Content (Join-Path $S.RunDir 'SUBMIT.md') -Encoding UTF8
}
function ZipRun{
 $parent=Split-Path -Parent $S.RunDir;$leaf=Split-Path -Leaf $S.RunDir;$tmp=Join-Path $parent ($leaf+'.tmp.zip');if(Test-Path $tmp){Remove-Item $tmp -Force}
 Compress-Archive -Path (Join-Path $S.RunDir '*') -DestinationPath $tmp -CompressionLevel Optimal;Move-Item $tmp (Join-Path $S.RunDir 'run.zip') -Force
}
function Partial($cfg){
 try{if($S.Loggers.Count){StopLoggers ([int]$cfg.logging.stopTimeoutMs) ([int]$cfg.logging.flushWaitMs)}}catch{}
 try{if($S.App-and-not$S.App.HasExited-and$cfg.application.stopOnFinish){$S.AppInfo.stopMode=StopProc $S.App 'application' ([int]$cfg.application.stopTimeoutMs) ([bool]$cfg.application.allowForceStop)}}catch{}
 try{MergeApp}catch{};try{Snapshot ([string]$cfg.dongle.instanceIdPattern) (Join-Path $S.RunDir 'device_after.txt')}catch{};try{GitInfo $S.Repo}catch{};try{Manifest $cfg;Submit $cfg}catch{}
}

if(-not(Test-Path $ConfigPath)){throw "Config not found: $ConfigPath"}
$cf=Get-Item $ConfigPath;$base=$cf.Directory.FullName;$cfg=Get-Content $cf.FullName -Raw|ConvertFrom-Json
$runs=RPath ([string]$cfg.output.runsDirectory) $base;RunId $runs

try{
 while($S.State-notin@('DONE','ABORTED')){
  try{
   switch($S.State){
    'INIT'{$S.Repo=RPath ([string]$cfg.repository.path) $base;State 'PRECHECK'}
    'PRECHECK'{
     if(-not(Test-Path (Join-Path $S.Repo '.git'))){throw 'Repository path is not a Git worktree'}
     $d=Exec git @('status','--porcelain') $S.Repo;if($d.Exit-ne0-or$d.Out.Count){throw 'Git worktree is dirty or git status failed'}
     $b=((Exec git @('branch','--show-current') $S.Repo).Out-join'').Trim();if($b-ne[string]$cfg.repository.branch){throw "Branch mismatch current=$b expected=$($cfg.repository.branch)"}
     Snapshot ([string]$cfg.dongle.instanceIdPattern) (Join-Path $S.RunDir 'device_before.txt')
     if(Dongle ([string]$cfg.dongle.instanceIdPattern)){Write-Host '対象ドングルを抜いてください。';if(-not(WaitDongle ([string]$cfg.dongle.instanceIdPattern) $false ([int]$cfg.dongle.pollIntervalMs) ([int]$cfg.timeouts.dongleDisconnectMs))){throw 'Dongle removal timeout'};Event 'DongleDisconnected' 'device' 'precheck'}
     foreach($n in @([string]$cfg.application.processName)){if($n-and@(Get-Process -Name $n -ErrorAction SilentlyContinue).Count){throw "Residual process: $n"}}
     State 'UPDATE'
    }
    'UPDATE'{
     $remote=[string]$cfg.repository.remote;$branch=[string]$cfg.repository.branch;$f=Exec git @('fetch',$remote) $S.Repo;if($f.Exit-ne0){throw 'git fetch failed'};$p=Exec git @('pull','--ff-only',$remote,$branch) $S.Repo;if($p.Exit-ne0){throw 'git pull --ff-only failed'}
     $head=((Exec git @('rev-parse','HEAD') $S.Repo).Out-join'').Trim();$short=((Exec git @('rev-parse','--short=7','HEAD') $S.Repo).Out-join'').Trim();$url=((Exec git @('remote','get-url',$remote) $S.Repo).Out-join'').Trim()
     $S.Git=[ordered]@{path=$S.Repo;remote=$remote;remoteUrl=$url;branch=$branch;commit=$head;shortCommit=$short;dirty=$false;pullResult=($p.Out-join[Environment]::NewLine);updatedAt=NowIso};RenameRun $short;GitInfo $S.Repo;Event 'RepositoryUpdated' 'git' "$branch@$short";State 'START_LOGGING'
    }
    'START_LOGGING'{StartLoggers $cfg.logging.processes $base;Event 'LoggingStarted' 'harness';State 'START_APPLICATION'}
    'START_APPLICATION'{
     $exe=RPath ([string]$cfg.application.path) $base;$wd=RPath ([string]$cfg.application.workingDirectory) $base;$S.App=StartProc $exe ([string]$cfg.application.arguments) $wd (Join-Path $S.RunDir 'application.stdout.log') (Join-Path $S.RunDir 'application.stderr.log')
     $S.AppInfo=[ordered]@{path=$exe;arguments=[string]$cfg.application.arguments;workingDirectory=$wd;pid=$S.App.Id;startTime=NowIso;stopMode=$null};Event 'ApplicationStarted' 'harness' '' '' $S.App.Id
     if([int]$cfg.application.startWaitMs-gt0){Start-Sleep -Milliseconds ([int]$cfg.application.startWaitMs);if($S.App.HasExited){throw 'Application exited during startup'}}
     State 'WAIT_DONGLE'
    }
    'WAIT_DONGLE'{Write-Host '対象USBドングルを挿してください。';if(-not(WaitDongle ([string]$cfg.dongle.instanceIdPattern) $true ([int]$cfg.dongle.pollIntervalMs) ([int]$cfg.timeouts.dongleConnectMs))){throw 'Dongle connect timeout'};$dev=(Pnp ([string]$cfg.dongle.instanceIdPattern)|select -First 1 -Expand InstanceId);Event 'DongleConnected' 'device' '' '' $null $dev;State 'WAIT_PAIR_1'}
    'WAIT_PAIR_1'{$r=Operator 'Trackerをペアリングしてください。';Event 'TrackerPairing1Completed' 'operator' '' $r;if($r-eq'aborted'){$S.Result='aborted';State 'ABORTED';break};if($r-eq'failed'){throw 'First pairing failed'};State 'WAIT_POWER_OFF'}
    'WAIT_POWER_OFF'{$r=Operator 'Trackerの電源を切ってください。';Event 'TrackerPowerOffCompleted' 'operator' '' $r;if($r-eq'aborted'){$S.Result='aborted';State 'ABORTED';break};if($r-eq'failed'){throw 'Power-off step failed'};State 'WAIT_PAIR_2'}
    'WAIT_PAIR_2'{$r=Operator 'Trackerを再度ペアリングしてください。' $true;Event 'TrackerPairing2Completed' 'operator' '' $r;if($r-eq'aborted'){$S.Result='aborted';State 'ABORTED';break};if($r-eq'failed'){throw 'Second pairing failed'};State 'STOP_LOGGING'}
    'STOP_LOGGING'{StopLoggers ([int]$cfg.logging.stopTimeoutMs) ([int]$cfg.logging.flushWaitMs);Event 'LoggingStopped' 'harness';if($cfg.application.stopOnFinish-and$S.App){$mode=StopProc $S.App 'application' ([int]$cfg.application.stopTimeoutMs) ([bool]$cfg.application.allowForceStop;$S.AppInfo.stopMode=$mode;$S.AppInfo.endTime=NowIso;Event 'ApplicationStopped' 'harness' $mode};State 'COLLECT'}
    'COLLECT'{MergeApp;Snapshot ([string]$cfg.dongle.instanceIdPattern) (Join-Path $S.RunDir 'device_after.txt');GitInfo $S.Repo;$S.Result='completed';Manifest $cfg;Submit $cfg;State 'PACKAGE'}
    'PACKAGE'{if($cfg.output.createZip){ZipRun;Event 'PackageCreated' 'harness' 'run.zip';Manifest $cfg;Submit $cfg};State 'DONE'}
    'ERROR'{$S.Result='failed';State 'COLLECT_PARTIAL'}
    'COLLECT_PARTIAL'{Partial $cfg;State 'DONE'}
   }
  }catch{$S.Failure=$_.Exception.Message;Log $S.Failure 'ERROR';Event 'HarnessError' 'harness' $S.Failure 'failed';if($S.State-notin@('ERROR','COLLECT_PARTIAL','DONE','ABORTED')){State 'ERROR'}else{throw}}
 }
}finally{
 if($S.State-eq'ABORTED'){$S.Result='aborted';Partial $cfg}elseif($S.State-ne'DONE'){if($S.Result-eq'running'){$S.Result='partial'};Partial $cfg}
 if($S.RunDir){try{Manifest $cfg;Submit $cfg}catch{};Write-Host "Run directory: $($S.RunDir)";Write-Host "Result: $($S.Result)"}
}
