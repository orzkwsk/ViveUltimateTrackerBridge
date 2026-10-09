"""Read-only Git provenance and static binary inventory; never opens hardware.

Output contains line numbers, hashes, and allowlisted symbol presence only.
It deliberately does not export source, arbitrary strings, RF, or serial values.
"""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess

PATTERNS = {
    "pair_control": r"PairDevice|beginPairing|endPairing|RF_BEHAVIOR_PAIR_DEVICE",
    "power_saving": r"RxPowerSaving|RX_POWER_SAVING|setRxPowerSaving",
    "rf_restart": r"RestartRf|RESTART_RF",
    "post_pair": r"configureOnPair|ARI1|ATM-1|ATM20|ATM11",
    "info_complete_reply": r"NAZZ|FW1",
    "info_request": r'ACK_NA|AKC_NA|request_device_info|sendAck\([^\n]*"NA"',
    "passive_guard": r"diagnosticPassiveCapture",
}
PATHS = ("src/backend.cpp", "src/pairing.cpp", "src/protocol.cpp",
         "include/vut/backend.hpp", "include/vut/protocol.hpp",
         "tools/vut_identity_capture.cpp", "tools/vut_debug_console.cpp")
BINARY_TOKENS = ("Pairing", "Unpair", "RestartRf", "RxPowerSaving", "NADS",
                 "NASS", "NAZZ", "FW1", "DeviceInfo", "con_bit_pairing",
                 "con_bit_connecting", "con_bit_rf_connected")


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args], text=True,
                                   encoding="utf-8", errors="strict").strip()


def refs_inventory(root):
    refs = git(root, "for-each-ref", "--format=%(refname) %(objectname)",
               "refs/heads", "refs/remotes/origin").splitlines()
    result = []
    blobs = {}
    for ref in refs:
        name, commit = ref.split()
        if name == "refs/remotes/origin/HEAD":
            continue
        tree_paths = set(git(root, "ls-tree", "-r", "--name-only", commit).splitlines())
        files = []
        for path in PATHS:
            if path not in tree_paths:
                continue
            blob = git(root, "rev-parse", f"{commit}:{path}")
            files.append({"path": path, "blob": blob})
            if blob not in blobs:
                source = git(root, "show", f"{commit}:{path}")
                blobs[blob] = {"path": path,
                               "matches": {key: [i for i, line in enumerate(source.splitlines(), 1)
                                                 if re.search(pattern, line)]
                                           for key, pattern in PATTERNS.items()}}
        result.append({"ref": name, "commit": commit, "files": files})
    changes = git(root, "log", "--all", "--format=%H", "-G",
                  "RxPowerSaving|RestartRf|configureOnPair|PairDevice|NAZZ|FW1|reconnect",
                  "--", "src/backend.cpp", "src/pairing.cpp", "include/vut/backend.hpp", "tools")
    return {"scope": "All locally available heads and fetched origin refs; reachable relevant history. Deleted/unavailable remote refs cannot be claimed audited.",
            "refs": result, "source_blobs": blobs, "reachable_change_commits": changes.splitlines()}


def binary_inventory(paths):
    result = []
    for path in paths:
        p = pathlib.Path(path)
        raw = p.read_bytes()
        strings = re.findall(rb"[ -~]{2,}", raw)
        utf16 = re.findall(rb"(?:[ -~]\x00){2,}", raw)
        all_strings = [s.decode("ascii") for s in strings]
        all_strings += [s.decode("utf-16-le") for s in utf16]
        result.append({"file": p.name, "bytes": len(raw),
                       "sha256": hashlib.sha256(raw).hexdigest(),
                       "token_presence": {t: any(t in s for s in all_strings)
                                          for t in BINARY_TOKENS},
                       "exact_NA_string_present": "NA" in all_strings,
                       "interpretation": "String presence/absence does not prove dispatch, causal ordering, wire bytes, or safety."})
    return result


def saved_rf_inventory(paths):
    """Reuse historical RF-status layout as diagnostic bytes, never state enums."""
    result = []
    for index, path in enumerate(paths, 1):
        rows = [json.loads(line) for line in pathlib.Path(path).read_text(encoding="utf-8-sig").splitlines()
                if line.strip()]
        entry = {"run": f"Run-{index}", "scenario": rows[0]["scenario"],
                 "rf_status_reports": 0, "malformed_rf_reports": 0, "transitions": [],
                 "interpretation": "Opaque historical diagnostic words; no SDK state mapping or physical tracker binding."}
        previous = None
        for row in rows:
            if row.get("kind") != "raw_report":
                continue
            wire = bytes.fromhex(row["raw_report_hex"])
            if wire[:2] in (b"\x00\x1d", b"\x00\x1e"):
                wire = wire[1:]
            if not wire or wire[0] not in (0x1d, 0x1e):
                continue
            if len(wire) < 6 or wire[1] < 4 or len(wire) < wire[1]:
                entry["malformed_rf_reports"] += 1
                continue
            payload = wire[4:wire[1]]
            if len(payload) < 2 or payload[1] < 23 or len(payload) < 2 + payload[1]:
                entry["malformed_rf_reports"] += 1
                continue
            inner = payload[2:2 + payload[1]]
            words = [f"0x{int.from_bytes(inner[3 + i * 4:7 + i * 4], 'little'):08x}" for i in range(5)]
            entry["rf_status_reports"] += 1
            key = (row["transport_generation"], tuple(words))
            if key != previous:
                entry["transitions"].append({"generation": key[0],
                                              "raw_report_index": row["raw_report_index"],
                                              "slot_words": words})
                previous = key
        result.append(entry)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=pathlib.Path)
    parser.add_argument("--binary", action="append", default=[])
    parser.add_argument("--captures", nargs="+", default=[])
    args = parser.parse_args()
    result = {"hardware_io": "NONE", "source_execution": "NONE"}
    if args.repo:
        result["git"] = refs_inventory(args.repo)
    if args.binary:
        result["binaries"] = binary_inventory(args.binary)
    if args.captures:
        result["saved_rf_status"] = saved_rf_inventory(args.captures)
    print(json.dumps(result, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
