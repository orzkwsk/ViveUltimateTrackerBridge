import json
from pathlib import Path
import subprocess
import sys
import tempfile

exe = sys.argv[1]
def check(condition, message):
    if not condition:
        raise RuntimeError(message)

with tempfile.TemporaryDirectory(prefix="vut-setup-cli-") as root:
    path = Path(root) / "assignments.json"
    def run(*args, commands=None):
        return subprocess.run([exe, str(path), *args], input=commands, capture_output=True, text=True)
    result = run("--validate")
    check(result.returncode == 0 and not path.exists(), result.stderr)
    # Seed is explicitly mock stable identity evidence, not discovered hardware.
    path.write_text(json.dumps({"schema_version": 1, "future": "keep", "assignments": {
        "serial:A": {"role": "LeftFoot", "extra": "keep"}, "serial:B": {"role": "RightFoot"}}}))
    result = run("--offline", commands="reassign serial:A RightFoot\nvalidate\nsave\nquit\n")
    check("Swap / Replace / Cancel" in result.stderr, result.stderr)
    check(json.loads(path.read_text())["assignments"]["serial:A"]["role"] == "LeftFoot", result.stdout)
    result = run("--offline", commands="reassign serial:A RightFoot swap\nsave\nrefresh\nquit\n")
    data = json.loads(path.read_text())
    check(result.returncode == 0 and data["assignments"]["serial:B"]["role"] == "LeftFoot", result.stderr)
    check(data["future"] == "keep" and data["assignments"]["serial:A"]["extra"] == "keep", data)
    check("retained" in result.stdout and "BLOCKED" in result.stdout, result.stdout)
    before = path.read_bytes()
    result = run("--offline", commands="clear-all\nreassign serial:A Unknown\nunassign serial:A\nquit\n")
    check("Unsaved" in result.stderr and path.read_bytes() == before, result.stderr)
    result = run("--offline", commands="swap serial:A serial:B\nunassign serial:A\nsave\nquit\n")
    check(list(json.loads(path.read_text())["assignments"]) == ["serial:B"], result.stdout)
    result = run("--offline", commands="clear-all confirm\nsave\nquit\n")
    check(json.loads(path.read_text())["assignments"] == {}, result.stdout)
    for data in ["{broken", '{"schema_version":1,"assignments":{"serial:A":{"role":"NoSuchRole"}}}',
                 '{"schema_version":1,"assignments":{"slot:1":{"role":"Waist"}}}']:
        path.write_text(data)
        result = run("--validate")
        check(result.returncode == 1 and path.read_text() == data, result.stderr)
print("PASS CLI conflicts/edit/save/disconnected retention/unknown preservation/invalid config; no HID opened")
