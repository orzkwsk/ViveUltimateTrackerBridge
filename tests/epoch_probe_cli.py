"""Hardware-free CLI, receipt allowlist and sender-boundary checks (also under -O)."""
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(binary, *arguments):
    return subprocess.run([binary, *arguments], capture_output=True, text=True, timeout=20)


probe, fixture = sys.argv[1:]
help_result = run(probe, "--help")
check(help_result.returncode == 0 and "ObserveOnly" in help_result.stdout and "exclusive HID" in help_result.stdout,
      "help/fixed configuration")
for args in [[], ["--legacy"], ["--pair"], ["--raw-command", "synthetic-secret"],
             ["--seconds", "91"], ["--seconds", "0"], ["--seconds", "-1"],
             ["--seconds", "90trailing"], ["--jsonl"], ["--help", "synthetic-secret"]]:
    result = run(probe, *args)
    check(result.returncode == 3 and "synthetic-secret" not in result.stdout + result.stderr,
          "invalid CLI reached hardware or leaked input")

with tempfile.TemporaryDirectory(dir=Path(probe).resolve().parent) as directory:
    control = Path(directory) / "control"
    stop = Path(directory) / "stop"
    control.write_text("b_on_confirmed\n", encoding="ascii")
    result = run(probe, "--control-file", str(control), "--stop-file", str(stop))
    check(result.returncode == 3 and "runner_started" not in result.stdout, "stale control reached hardware")
    check(str(control) not in result.stdout + result.stderr, "control path leaked")
    result = run(probe, "--control-file", str(stop), "--stop-file", str(stop))
    check(result.returncode == 3, "aliased control paths")

emitted = run(fixture, "--emit-fixture")
check(emitted.returncode == 0 and not emitted.stderr, "offline fixture failed")
rows = [json.loads(line) for line in emitted.stdout.splitlines()]
allowed = {"seq", "elapsed_ms", "kind", "generation", "epoch", "slot", "phase", "progress", "seen_mask",
           "active", "info_complete", "initialization_eligible", "pose_observed", "reason", "label"}
check(all(set(row) <= allowed for row in rows), "non-allowlisted receipt field")
check([row["seq"] for row in rows] == list(range(1, len(rows) + 1)), "receipt sequence")
labels = [row["label"] for row in rows if row["kind"] == "ack"]
check(labels[:9] == ["NANA", "NADS", "NASS", "NASI", "NAPI", "NAV1", "NARI", "NAGN", "NAZZ"], "token labels")
check(set(labels) <= {"NANA", "NADS", "NASS", "NASI", "NAPI", "NAV1", "NARI", "NAGN", "NAZZ",
                     "ACK_OTHER", "ACK_UNKNOWN_N"}, "raw ACK leakage")
check(not re.search(r"(?:[0-9a-f]{2}:){5}[0-9a-f]{2}", emitted.stdout, re.I), "DeviceId leakage")
epoch = next(row for row in rows if row["kind"] == "epoch")
check(epoch["epoch"] == 19 and epoch["generation"] == 8 and epoch["progress"] == 9
      and epoch["seen_mask"] == 511 and epoch["info_complete"] and epoch["initialization_eligible"], "epoch JSON")

root = Path(__file__).resolve().parents[1]
sources = "\n".join((root / "tools" / name).read_text(encoding="utf-8")
                    for name in ["vut_epoch_probe.cpp", "vut_epoch_probe_support.hpp"])
for forbidden in [r"\b(?:beginPairing|endPairing|endPairingWithRetry|setFeature|getFeature|sendAck|sendCommand)\s*\(",
                  r"\bbuild(?:AckTx|FeatureCommand|RfBehaviorPayload)\s*\(", r"\.toString\s*\(",
                  r"\.mac\b", r"\.what\s*\(", r"\.lastError\s*\("]:
    check(not re.search(forbidden, sources), "runner sender/raw identity boundary")
print("PASS hardware-free epoch probe CLI, JSON allowlist, redaction and explicit sender boundary")
