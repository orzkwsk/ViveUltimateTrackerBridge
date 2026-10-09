"""Read saved NDJSON only; never opens HID, sends commands, or verifies a provider.

Public output is a field allowlist. Byte aliases preserve equality across namespaces
via equality_token; unknown ACK prefixes and user metadata never pass through.
"""
import argparse
import json
from pathlib import Path
import re
import sys

IDENTITIES = ("NADS", "NASS")
PREFIXES = ("NANA", "NADS", "NASS", "NASI", "NAPI", "NAV1", "NAV", "NAZZ", "NAGN", "NARI")
SCENARIOS = ("HIL-01", "HIL-02", "HIL-03", "HIL-04", "HIL-05", "HIL-06-before", "HIL-06-after", "HIL-07")
LABELS = ("Tracker A", "Tracker B")


class Aliases:
    def __init__(self):
        self.values = {}

    def get(self, category, value):
        table = self.values.setdefault(category, {})
        if value not in table:
            table[value] = f"{category}-{len(table) + 1}"
        return table[value]


def hex_bytes(value):
    if not isinstance(value, str) or not re.fullmatch(r"(?:[0-9a-fA-F]{2})*", value):
        raise ValueError("invalid_hex")
    return bytes.fromhex(value)


def address(value):
    if not isinstance(value, str) or not re.fullmatch(r"(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}", value):
        raise ValueError("invalid_address")
    return bytes.fromhex(value.replace(":", ""))


def integer(value, minimum=0):
    if type(value) is not int or value < minimum:
        raise ValueError("invalid_integer")
    return value


def envelope(raw):
    wire = hex_bytes(raw["raw_report_hex"])
    if wire[:2] == b"\x00\x28":
        wire = wire[1:]
    if len(wire) < 12 or wire[0] != 0x28 or len(wire) < 12 + wire[11]:
        raise ValueError("invalid_envelope")
    return wire[3:9], wire[4] & 15, int.from_bytes(wire[9:11], "little"), wire[12:12 + wire[11]]


def analyze(records, bindings, burst_gap_ns=2_000_000_000):
    """Intervals group observations, not a vendor definition of a complete burst.

    Keep input order within a run. Never sort malformed/reordered evidence into a
    passing result. Generation, transport loss, unpair, pair and slot replacement
    retire live candidate associations. Historical values only detect mutation.
    """
    integer(burst_gap_ns, 1)
    aliases = Aliases()
    failures, insufficient = set(), set()
    raws, normalized_bindings = {}, []
    for b in bindings:
        try:
            if b.get("physical_confirmation") is not True or b.get("physical_tracker_label") not in LABELS:
                raise ValueError("unconfirmed_binding")
            first = integer(b["first_raw_report_index"], 1)
            last = integer(b["last_raw_report_index"], first)
            if not isinstance(b["capture_run_id"], str) or not b["capture_run_id"]:
                raise ValueError("invalid_run")
            normalized_bindings.append((b["capture_run_id"], integer(b["transport_generation"], 1),
                                        address(b["rf_address"]), b["scenario"], first, last,
                                        b["physical_tracker_label"]))
        except (KeyError, TypeError, ValueError):
            insufficient.add("invalid_human_binding")
    for r in records:
        if r.get("kind") != "raw_report":
            continue
        key = (r.get("capture_run_id"), r.get("transport_generation"), r.get("raw_report_reference"))
        if key in raws:
            failures.add("duplicate_raw_reference")
        raws[key] = r

    events, bursts, live, slots, previous, generations = [], [], {}, {}, {}, {}
    histories, seen, run_summaries, active_bursts = {}, set(), {}, {}
    for r in records:
        run = r.get("capture_run_id")
        if not isinstance(run, str) or not run:
            insufficient.add("missing_run")
            continue
        run_alias = aliases.get("Run", run)
        summary = run_summaries.setdefault(run_alias, {"run": run_alias, "scenario": "UNKNOWN",
            "counts": {}, "pose_order": "UNKNOWN_NOT_RECORDED", "evidence_kind": "unknown"})
        marker = r.get("scenario")
        if marker not in SCENARIOS:
            insufficient.add("missing_or_unknown_scenario_marker")
            marker = "UNKNOWN"
        if summary["scenario"] not in ("UNKNOWN", marker):
            failures.add("scenario_changed_within_run")
        summary["scenario"] = marker
        kind = r.get("kind")
        if kind in ("capture_start", "capture_stop", "transport_open", "transport_open_failed", "transport_lost", "raw_report", "ack", "pair", "unpair"):
            summary["counts"][kind] = summary["counts"].get(kind, 0) + 1
        evidence = r.get("evidence_kind")
        evidence = evidence if evidence in ("synthetic", "hardware-passive") else "unknown"
        if summary["evidence_kind"] == "unknown":
            summary["evidence_kind"] = evidence
        elif summary["evidence_kind"] != evidence:
            failures.add("mixed_evidence_kind")
        gen = r.get("transport_generation")
        if gen is not None:
            try:
                gen = integer(gen, 1)
            except ValueError:
                failures.add("invalid_generation")
                continue
            if run in generations and gen < generations[run]:
                failures.add("retired_generation_record")
                continue
            if generations.get(run) != gen or kind == "transport_lost":
                live = {k: v for k, v in live.items() if k[0] != run}
                slots = {k: v for k, v in slots.items() if k[0] != run}
                active_bursts = {k: v for k, v in active_bursts.items() if k[0] != run}
            generations[run] = gen
        if kind not in ("ack", "pair", "unpair"):
            continue
        try:
            gen = integer(gen, 1)
            rf = address(r["rf_address"])
            slot = integer(r["slot"])
            index = integer(r["raw_report_index"], 1)
            ns_time = integer(r["observed_monotonic_ns"])
            if slot != rf[1] & 15:
                raise ValueError("slot_mismatch")
            old_index, old_time = previous.get(run, (0, -1))
            if index < old_index or ns_time < old_time:
                raise ValueError("nonmonotonic_event_order")
            previous[run] = (index, ns_time)
            raw = raws[(run, gen, r["raw_report_reference"])]
            if any(raw.get(f) != r.get(f) for f in ("scenario", "observed_monotonic_ns", "raw_report_index", "evidence_kind")):
                raise ValueError("raw_metadata_mismatch")
            if kind == "ack":
                actual_rf, actual_slot, actual_type, ack = envelope(raw)
                if (actual_rf, actual_slot, actual_type) != (rf, slot, 0x0101) or ack != hex_bytes(r["raw_ack_hex"]):
                    raise ValueError("raw_ack_mismatch")
                prefix = next((p for p in PREFIXES if ack.startswith(p.encode())), "N_OTHER" if ack.startswith(b"N") else "OTHER")
                payload = ack[len(prefix):] if prefix in PREFIXES else ack
                if prefix in IDENTITIES and (r.get("candidate_namespace") != prefix or hex_bytes(r["candidate_value_hex"]) != payload):
                    raise ValueError("candidate_parser_mismatch")
            else:
                wire = hex_bytes(raw["raw_report_hex"])
                if wire[:2] == b"\x00\x18":
                    wire = wire[1:]
                if len(wire) < 12 or wire[0] != 0x18 or wire[1] < 12 or len(wire) < wire[1] or wire[6:12] != rf or bool(wire[5]) != (kind == "unpair"):
                    raise ValueError("raw_pair_mismatch")
                prefix, payload = kind, None
        except (KeyError, TypeError, ValueError):
            failures.add("malformed_or_unproven_event")
            continue
        matches = [b for b in normalized_bindings if b[:4] == (run, gen, rf, marker) and b[4] <= index <= b[5]]
        label = matches[0][6] if len(matches) == 1 else "UNCONFIRMED"
        if len(matches) > 1:
            failures.add("ambiguous_human_binding")
        if label == "UNCONFIRMED":
            insufficient.add("missing_human_binding")
        scope = (run, gen, rf)
        slot_key = (run, gen, slot)
        old = slots.get(slot_key)
        if old and old != scope:
            if kind == "pair":
                live.pop(old, None)
                active_bursts.pop(old, None)
            else:
                # An ACK alone cannot prove the previous address disconnected.
                failures.add("unproven_slot_replacement")
        if kind in ("pair", "unpair"):
            live.pop(scope, None)
            active_bursts.pop(scope, None)
        if kind == "unpair":
            if old == scope:
                slots.pop(slot_key, None)
        else:
            slots[slot_key] = scope
        base = dict(run=run_alias, scenario=marker, generation=gen, RF=aliases.get("RF", rf), slot=slot,
                    physical_tracker=label, event=prefix, raw_report_index=index)
        if prefix in IDENTITIES:
            if not payload or any(c < 32 or c >= 127 for c in payload):
                failures.add("empty_or_nonprintable_candidate")
            current = live.setdefault(scope, {})
            if prefix in current and current[prefix] != payload:
                failures.add("candidate_changed_current_binding")
            if any(k != scope and k[:2] == scope[:2] and v.get(prefix) == payload for k, v in live.items()):
                failures.add("duplicate_live_candidate")
            current[prefix] = payload
            if label != "UNCONFIRMED":
                history = histories.setdefault((label, prefix), {})
                history.setdefault(payload, set()).add((run, gen, rf, slot))
                seen.add((label, marker, prefix))
                if len(history) > 1:
                    failures.add("physical_tracker_candidate_mutation")
            base.update(candidate=aliases.get(prefix, payload), equality_token=aliases.get("Bytes", payload))
        events.append((base, ns_time))
        if kind == "ack" and ack.startswith(b"N"):
            burst = active_bursts.get(scope)
            if burst is None or ns_time - burst["last_ns"] > burst_gap_ns:
                burst = dict(base, events=[], first_ns=ns_time, last_ns=ns_time)
                bursts.append(burst)
                active_bursts[scope] = burst
            burst["events"].append(dict(base, relative_ns=ns_time - burst["first_ns"],
                                         interval_ns=ns_time - burst["last_ns"]))
            burst["last_ns"] = ns_time
    # Reference time per run/generation. Absolute host timestamps never become IDs.
    starts = {}
    timeline = []
    for row, ns_time in events:
        origin = starts.setdefault((row["run"], row["generation"]), ns_time)
        timeline.append(dict(row, relative_ns=ns_time - origin))
    public_bursts = []
    for b in bursts:
        public_bursts.append({k: b[k] for k in ("run", "scenario", "generation", "RF", "slot", "physical_tracker", "events")} |
                             {"duration_ns": b["last_ns"] - b["first_ns"],
                              "missing_identity_namespaces": [ns for ns in IDENTITIES if not any(e["event"] == ns for e in b["events"])],
                              "completeness": "UNKNOWN_VENDOR_BOUNDARY"})
    candidates = {}
    for ns in IDENTITIES:
        tracker_rows = {}
        for label in LABELS:
            history = histories.get((label, ns), {})
            contexts = set().union(*history.values()) if history else set()
            tracker_rows[label] = {"observed_values": len(history),
                "stability": "MUTATION" if len(history) > 1 else ("OBSERVED_EQUAL" if history else "UNKNOWN"),
                "RF_count": len({c[2] for c in contexts}), "slot_count": len({c[3] for c in contexts}),
                "run_generation_count": len({c[:2] for c in contexts})}
        missing = [f"{label}/{scenario}" for label in LABELS for scenario in SCENARIOS if (label, scenario, ns) not in seen]
        if missing:
            insufficient.add("missing_required_scenario_evidence")
        candidates[ns] = {"class": "B_HIL_REQUIRED", "trackers": tracker_rows, "missing": missing,
                          "acquisition": "UNRESOLVED", "production": "NOT_AUTHORIZED"}
    return {"status": "FAIL_CLOSED" if failures else ("INSUFFICIENT_EVIDENCE" if insufficient else "OBSERVATIONS_ONLY"),
            "failures": sorted(failures), "insufficient": sorted(insufficient), "runs": list(run_summaries.values()),
            "timeline": timeline, "bursts": public_bursts, "candidates": candidates,
            "burst_gap_ns": burst_gap_ns, "hardware_checks_this_analysis": "NOT_RUN",
            "production_integration_readiness": "NOT_AUTHORIZED_BY_THIS_TOOL"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", type=Path, nargs="+")
    parser.add_argument("--bindings", required=True, type=Path)
    args = parser.parse_args()
    try:
        records = [json.loads(line) for p in args.captures for line in p.read_text(encoding="utf-8-sig").splitlines() if line.strip()]
        if any(not isinstance(r, dict) for r in records):
            raise ValueError("invalid_record")
        bindings = json.loads(args.bindings.read_text(encoding="utf-8-sig"))["bindings"]
        if not isinstance(bindings, list) or any(not isinstance(b, dict) for b in bindings):
            raise ValueError("invalid_bindings")
        result = analyze(records, bindings)
        print(json.dumps(result, ensure_ascii=True, indent=2))
        return 2 if result["status"] == "FAIL_CLOSED" else 0
    except (OSError, ValueError, KeyError, TypeError):
        # Do not echo paths, malformed NDJSON or exception text containing raw IDs.
        print('{"status":"FAIL_CLOSED","failures":["invalid_input"]}')
        return 2


if __name__ == "__main__":
    sys.exit(main())
