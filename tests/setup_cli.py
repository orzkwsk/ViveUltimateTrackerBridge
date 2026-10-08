import subprocess
import sys
import tempfile
from pathlib import Path

exe = sys.argv[1]
def check(value, message):
    if not value: raise RuntimeError(message)
def run(commands):
    return subprocess.run([exe, "--offline"], input=commands, capture_output=True, text=True)
result = run("help\npair-status\nrefresh\nidentify\nnext\nquit\n")
check(result.returncode == 0, result.stderr)
check("pair [all|0|1|2|3|4]" in result.stdout and "HIL_REQUIRED" in result.stdout, result.stdout)
for word in ["LeftFoot", "RightFoot", "Waist", "Chest", "schema_version", "reassign"]:
    check(word not in result.stdout, word)
for commands in ["roles", "save", "assign serial:A LeftFoot", 'confirm "unterminated',
                 "pair 5", "pair all", "unknown", "refresh extra", "bad\x00input", "x"*4097]:
    result = run(commands+"\npair-status\nquit\n")
    check(result.returncode != 0 and "ERROR:" in result.stderr, result)
    # Subsequent successful commands cannot mask an earlier script failure.
    check("pair_state=" in result.stdout, result.stdout)
with tempfile.TemporaryDirectory(prefix="vut-production-cli-") as root:
    path = Path(root)/"assignments.json"
    result = subprocess.run([exe, str(path), "--offline"], capture_output=True, text=True)
    check(result.returncode != 0 and not path.exists(), result)
print("PASS setup CLI script exits, malformed input, no production body-role schema; no HID opened")
