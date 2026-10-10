"""Only inert argument rejection and stale locks; no valid hardware invocation."""
from pathlib import Path
import subprocess
import sys
import tempfile

def check(value, message):
    if not value: raise RuntimeError(message)
def run(args): return subprocess.run([sys.argv[1],*args],capture_output=True,text=True,timeout=10)
p=run(['--help'])
check(p.returncode==0 and 'Default: observe only' in p.stdout and 'Behavior2 inaccessible' in p.stdout,'Help/default guard')
check('Only after NormalShutdown: GUI Stop/save, then offline capture audit.' in p.stdout,
      'GUI capture shutdown ordering guidance')
for args in ([],['--q1e-once'],['--behavior2'],['--send-behavior2-once'],['--retry'],['--auto'],['--all'],['--sequence'],['--observe','--observe']):
    p=run(args);check(p.returncode==3 and 'ExclusiveOpenSucceeded' not in p.stdout,'Startup sender exposed')
with tempfile.TemporaryDirectory() as parent:
    directory=Path(parent)/'run';directory.mkdir();(directory/'owner.lock').write_text('old-run\n')
    p=run(['--observe','--run-dir',str(directory)])
    check(p.returncode==3 and not (directory/'runner-private.jsonl').exists(),'Owner journal reuse reached hardware')
    second=Path(parent)/'second';second.mkdir();(Path(parent)/'q1e-active-trial.lock').write_text('consumed\n')
    p=run(['--observe','--run-dir',str(second)])
    check(p.returncode==3 and not (second/'owner.lock').exists(),'Second process/trial reached hardware')
print('PASS startup send/Behavior2 rejection and persistent owner/trial lock rejection before hardware')
