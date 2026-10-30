"""Validate the actual default-runtime Publisher capture, including immutable receive time."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


exe = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="altra-runtime-replay-") as directory:
    output = Path(directory) / "publisher.jsonl"
    session = "00000000-0000-4000-8000-000000000011"
    args = [str(exe), str(output), session, "7"]
    result = subprocess.run(args, capture_output=True, text=True, timeout=15)
    require(result.returncode == 0, result.stdout + result.stderr)
    print(result.stdout, end="")
    rows = [json.loads(line) for line in output.read_text().splitlines()]
    require(len(rows) == 98, "quarantined frames must not be exported")
    epochs = set()
    modes = {}
    times = []
    for sequence, row in enumerate(rows):
        pose = row["envelope"]
        received = row["received_at_ns"]
        epochs.add(received - int(pose["timestamp_ns"]))
        require(int(pose["sequence"]) == sequence, "pose sequence advanced during quarantine")
        require(pose["session_id"] == pose["clock_id"] == session, "session changed")
        require(pose["coordinate_space"] == {"id": "p0-altra-map", "convention": "vut-native-v1", "revision": 7}, "mapping changed")
        require(pose["timestamp_kind"] == "receive", "timestamp is not receive evidence")
        require(pose["source_id"] == "p0-synthetic-altra", "wrong primary source")
        require(int(pose["sent_at_ns"]) >= int(pose["timestamp_ns"]), "send before receive")
        mode = pose["modality"]
        modes[mode] = modes.get(mode, 0) + 1
        require(pose["validity"]["position"] == (mode == "full"), "fabricated position")
        require(pose["validity"]["orientation"] == (mode != "none"), "fabricated orientation")
        require(pose["orientation_evidence"] == ("none" if mode == "none" else "device"), "orientation evidence")
        times.append(received)
    require(len(epochs) == 1, "immutable receive timestamp changed")
    require(modes == {"full": 80, "rotation_only": 8, "none": 10}, "raw optical status changed")
    intervals = [b - a for a, b in zip(times, times[1:])]
    require(all(value > 0 for value in intervals), "regressed receive timestamp")
    require(intervals[39] >= 600_000_000, "measured600ms silence absent")
    require(sum(value > 500_000_000 for value in intervals) == 1, "unexpected additional long gap")
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    refused = subprocess.run(args, capture_output=True, text=True, timeout=5)
    require(refused.returncode != 0 and "refusing to replace" in refused.stderr, "overwrite accepted")
    require(hashlib.sha256(output.read_bytes()).hexdigest() == digest, "existing evidence was changed")
    print("PASS exact Publisher capture:98rows, one measured silence, immutable epoch, FULL80/ROTATION_ONLY8/NONE10, overwrite refused")
