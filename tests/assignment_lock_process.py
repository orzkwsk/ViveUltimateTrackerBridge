import json
from pathlib import Path
import subprocess
import sys
import tempfile

worker, lab = sys.argv[1:]
def check(value, message):
    if not value: raise RuntimeError(message)
with tempfile.TemporaryDirectory(prefix="vut-lock-process-") as root:
    path = Path(root)/"sidecar.json"
    original = json.dumps({"schema_version":1,"assignments":{}})
    path.write_text(original)
    def save():
        return subprocess.run([lab,str(path),"--offline","--labels","fixture-label"],
                              input="save\nquit\n",capture_output=True,text=True,timeout=10)
    for crash in [False, True]:
        owner = subprocess.Popen([worker,"--hold-lock",str(path)],stdin=subprocess.PIPE,
                                 stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            check(owner.stdout.readline().strip() == "LOCKED", "worker failed to acquire lock")
            before = path.read_bytes()
            result = save()
            check(result.returncode != 0 and path.read_bytes() == before, result.stderr)
            if crash:
                owner.kill()  # only the test-owned helper
                owner.wait(timeout=10)
            else:
                owner.communicate("release\n",timeout=10)
                check(owner.returncode == 0, "lock helper failed")
            check(Path(str(path)+".lock").exists(), "persistent lock inode should remain")
            result = save()
            check(result.returncode == 0, result.stderr)
        finally:
            if owner.poll() is None:
                owner.kill()
                owner.wait(timeout=10)
print("PASS concurrent writer rejection and OS lock recovery after orderly exit and crash")
