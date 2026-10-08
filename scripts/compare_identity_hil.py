"""Offline diagnostic comparison. Never queries hardware or creates StableIdentity.

Raw identifiers stay private. Reports use equality-preserving aliases, not hashes.
Human bindings are bounded by run, generation, RF and raw-report interval.
"""
import argparse
import json
from pathlib import Path

NAMES = ("NADS", "NASS")
REQUIRED = ("HIL-01", "HIL-02", "HIL-03", "HIL-04", "HIL-05", "HIL-06-before", "HIL-06-after", "HIL-07")


def compare(records, bindings):
    issues, rows, observed = [], {}, {}
    namespace_issues = {ns: [] for ns in NAMES}
    aliases, rf_aliases = {}, {}
    live, slots, seen_rf = {}, {}, {}
    pair_order = {}
    run_kinds = {}
    raw_reports = {}
    for r in records:
        run = r["capture_run_id"]
        run_kinds.setdefault(run, set()).add(r.get("evidence_kind", "unknown"))
        if r.get("kind") == "raw_report":
            key = (run, r.get("transport_generation"), r.get("raw_report_reference"))
            if key in raw_reports: issues.append("duplicate_raw_reference")
            raw_reports[key] = r
    for b in bindings:
        if b.get("physical_confirmation") is not True or b.get("physical_tracker_label") not in ("Tracker A", "Tracker B"):
            raise ValueError("binding requires explicit physical confirmation of Tracker A/B")
        if not isinstance(b.get("first_raw_report_index"), int) or not isinstance(b.get("last_raw_report_index"), int) or b["first_raw_report_index"] < 1 or b["last_raw_report_index"] < b["first_raw_report_index"]:
            raise ValueError("binding requires a bounded raw-report interval")
        for field in ("capture_run_id", "transport_generation", "rf_address", "scenario"):
            if field not in b: raise ValueError("binding missing " + field)
    def alias(value, namespace):
        key = (namespace, value)
        if key not in aliases:
            aliases[key] = namespace + "-" + str(1 + sum(k[0] == namespace for k in aliases))
        return aliases[key]
    for r in records:
        kind = r.get("kind")
        if kind not in ("ack", "pair", "unpair"): continue
        run, gen, rf = r["capture_run_id"], r["transport_generation"], r["rf_address"]
        scope, address = (run, gen), (run, gen, rf)
        slot = r["slot"]
        if kind == "unpair":
            live.pop(address, None)
            if slots.get((scope, slot)) == address: slots.pop((scope, slot), None)
            continue
        old = slots.get((scope, slot))
        if old and old != address: live.pop(old, None)
        slots[(scope, slot)] = address
        if kind == "pair":
            live.pop(address, None)
            confirmed = [b for b in bindings if b["capture_run_id"] == run and b["transport_generation"] == gen
                         and b["rf_address"] == rf and b["scenario"] == r["scenario"]
                         and b["first_raw_report_index"] <= r["raw_report_index"] <= b["last_raw_report_index"]]
            if len(confirmed) == 1:
                pair_order.setdefault((r["scenario"], run, gen), []).append(confirmed[0]["physical_tracker_label"])
            continue
        ns = r.get("candidate_namespace")
        if ns not in NAMES: continue
        def problem(message): namespace_issues[ns].append(message)
        raw = raw_reports.get((run, gen, r.get("raw_report_reference")))
        if raw is None or raw.get("observed_monotonic_ns") != r.get("observed_monotonic_ns") or raw.get("raw_report_index") != r.get("raw_report_index"):
            problem("missing_or_mismatched_raw_evidence")
        # Compare bytes, never trimmed/converted/case-folded representations.
        value = r["candidate_value_hex"]
        payload = bytes.fromhex(value)
        ack = bytes.fromhex(r["raw_ack_hex"])
        if ack != ns.encode("ascii") + payload: problem("parser_raw_mismatch")
        if raw is not None:
            wire = bytes.fromhex(raw["raw_report_hex"])
            if wire[:2] == b"\x00\x28": wire = wire[1:]
            valid = len(wire) >= 12 and wire[0] == 0x28 and wire[9:11] == b"\x01\x01"
            if valid:
                actual_rf = ":".join(f"{c:02x}" for c in wire[3:9])
                valid = (actual_rf == rf and (wire[4] & 15) == slot and
                         len(wire) >= 12 + wire[11] and wire[12:12+wire[11]] == ack)
            if not valid: problem("raw_envelope_ack_mismatch")
        if not payload or any(c < 32 or c >= 127 for c in payload): problem("empty_or_nonprintable_candidate")
        key = (address, ns)
        seen_rf.setdefault(key, set()).add(value)
        if len(seen_rf[key]) > 1: problem("multiple_values_same_rf_namespace")
        if len({len(bytes.fromhex(v)) for v in seen_rf[key]}) > 1: problem("observed_length_changed")
        current = live.setdefault(address, {})
        current[ns] = value
        if any(other != address and other[:2] == address[:2] and data.get(ns) == value for other, data in live.items()):
            problem("duplicate_live_candidate")
        matches = [b for b in bindings if b["capture_run_id"] == run and b["transport_generation"] == gen
                   and b["rf_address"] == rf and b["scenario"] == r["scenario"]
                   and b["first_raw_report_index"] <= r["raw_report_index"] <= b["last_raw_report_index"]]
        if len(matches) > 1: problem("overlapping_physical_bindings")
        label = matches[0]["physical_tracker_label"] if len(matches) == 1 else "UNCONFIRMED"
        if label == "UNCONFIRMED": problem("unconfirmed_physical_label")
        if rf not in rf_aliases: rf_aliases[rf] = "RF-" + str(len(rf_aliases) + 1)
        group = (label, r["scenario"], run, gen, rf, slot)
        row = rows.setdefault(group, {"NADS": set(), "NASS": set(), "hex_values": {ns: set() for ns in NAMES}})
        row[ns].add(alias(value, ns))
        row["hex_values"][ns].add(value)
        observed.setdefault((label, r["scenario"], ns), set()).add(value)
    outcomes = {}
    for ns in NAMES:
        missing = [label + "/" + scenario for label in ("Tracker A", "Tracker B") for scenario in REQUIRED
                   if not observed.get((label, scenario, ns))]
        all_values = {label: set().union(*(observed.get((label, scenario, ns), set()) for scenario in REQUIRED))
                      for label in ("Tracker A", "Tracker B")}
        stable = all(len(v) == 1 for v in all_values.values())
        unique = bool(all_values["Tracker A"] and all_values["Tracker B"] and not (all_values["Tracker A"] & all_values["Tracker B"]))
        rf_change = {}
        for label in ("Tracker A", "Tracker B"):
            before = {key[4] for key in rows if key[0] == label and key[1] == "HIL-06-before" and rows[key][ns]}
            after = {key[4] for key in rows if key[0] == label and key[1] == "HIL-06-after" and rows[key][ns]}
            rf_change[label] = len(before) == len(after) == 1 and before != after
        # Accept an actual slot change or a recorded, physically confirmed reversal
        # of connection order. A scenario name alone is not evidence of either.
        order_change = {}
        baseline_orders = {tuple(labels) for key, labels in pair_order.items() if key[0] == "HIL-01" and len(labels) == 2 and len(set(labels)) == 2}
        reordered_orders = {tuple(labels) for key, labels in pair_order.items() if key[0] == "HIL-05" and len(labels) == 2 and len(set(labels)) == 2}
        connection_order_changed = any(tuple(reversed(order)) in reordered_orders for order in baseline_orders)
        for label in ("Tracker A", "Tracker B"):
            baseline = {key[5] for key in rows if key[0] == label and key[1] == "HIL-01" and rows[key][ns]}
            reordered = {key[5] for key in rows if key[0] == label and key[1] == "HIL-05" and rows[key][ns]}
            order_change[label] = bool(baseline and reordered and baseline != reordered)
        hardware = bool(run_kinds) and all(k == {"hardware-passive"} for k in run_kinds.values())
        reconnect = {}
        restart = {}
        for label in ("Tracker A", "Tracker B"):
            reconnect_scopes = {(key[2], key[3]) for key in rows if key[0] == label and key[1] == "HIL-04" and rows[key][ns]}
            reconnect[label] = any(sum(run == other_run for other_run, _ in reconnect_scopes) >= 2 for run, _ in reconnect_scopes)
            before_runs = {key[2] for key in rows if key[0] == label and key[1] == "HIL-01" and rows[key][ns]}
            after_runs = {key[2] for key in rows if key[0] == label and key[1] == "HIL-02" and rows[key][ns]}
            restart[label] = bool(before_runs and after_runs and before_runs.isdisjoint(after_runs))
        eligible = not missing and not issues and not namespace_issues[ns] and stable and unique and all(rf_change.values()) and (all(order_change.values()) or connection_order_changed) and all(reconnect.values()) and all(restart.values()) and hardware
        outcomes[ns] = {"class": "A_ELIGIBLE_REVIEW" if eligible else "B_HIL_REQUIRED", "missing": missing,
                        "stable": stable, "A_B_unique": unique, "re_pair_RF_changed": rf_change,
                        "slot_change_observed": order_change, "hardware_evidence": hardware,
                        "connection_order_changed": connection_order_changed,
                        "restart_new_run": restart, "dongle_generation_changed": reconnect,
                        "issues": sorted(set(namespace_issues[ns])),
                        "observed_lengths_bytes": sorted({len(bytes.fromhex(v)) for key, values in seen_rf.items() if key[1] == ns for v in values})}
    table = []
    for key, values in rows.items():
        label, scenario, run, gen, rf, slot = key
        a, b = values["hex_values"]["NADS"], values["hex_values"]["NASS"]
        relation = "unavailable" if not a or not b else ("equal" if len(a) == len(b) == 1 and a == b else ("different" if a.isdisjoint(b) else "mixed"))
        table.append({"physical_tracker": label, "scenario": scenario, "RF": rf_aliases[rf], "slot": slot,
                      "NADS": sorted(values["NADS"]), "NASS": sorted(values["NASS"]), "generation": gen,
                      "capture_run_id": run, "NADS_NASS_relation": relation,
                      "result": "OBSERVED" if label != "UNCONFIRMED" else "UNCONFIRMED"})
    return {"rows": table, "candidates": outcomes, "issues": sorted(set(issues + sum(namespace_issues.values(), []))),
            "production_integration_readiness": "NOT_AUTHORIZED_BY_THIS_TOOL"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", nargs="+", type=Path)
    parser.add_argument("--bindings", required=True, type=Path)
    args = parser.parse_args()
    records = []
    for path in args.captures:
        with path.open(encoding="utf-8") as stream:
            records.extend(json.loads(line) for line in stream if line.strip())
    bindings = json.loads(args.bindings.read_text(encoding="utf-8"))["bindings"]
    print(json.dumps(compare(records, bindings), ensure_ascii=True, indent=2))


if __name__ == "__main__":
    main()
