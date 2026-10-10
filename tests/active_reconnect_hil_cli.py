"""Only argument rejection and stale owner lock: NEVER launches hardware path."""
from pathlib import Path
import subprocess
import sys
import tempfile

binary = sys.argv[1]
def check(value, message):
    if not value:
        raise RuntimeError(message)
def run(args):
    return subprocess.run([binary, *args], capture_output=True, text=True, timeout=10)
p = run(["--help"])
check(p.returncode == 0 and "Default: observe only" in p.stdout, "Default/help guard")
for args in ([], ["--retry"], ["--send-behavior2-once"], ["--send-query1e-once"], ["--auto-reconnect"],
             ["--observe", "--observe"], ["--replay-official-sequence"], ["--repeat", "2"]):
    p = run(args)
    check(p.returncode == 3 and "ExclusiveOpenSucceeded" not in p.stdout, "Unexpected startup capability")
with tempfile.TemporaryDirectory() as directory:
    (Path(directory) / "owner.lock").write_text("old-run\n")
    p = run(["--observe", "--run-dir", directory])
    check(p.returncode == 3 and "InfrastructureFailureNoRecovery" in p.stderr and
          not (Path(directory) / "runner-private.jsonl").exists(), "Stale run lock did not fail before HID open")
print("PASS inert help, startup send rejection, stale run rejection before hardware path")
