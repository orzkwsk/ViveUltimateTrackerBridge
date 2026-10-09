"""Synthetic offline evidence; never opens devices or establishes hardware PASS."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("offline", Path(__file__).parents[1] / "scripts/analyze_identity_offline.py")
offline = importlib.util.module_from_spec(spec)
spec.loader.exec_module(offline)


def event(prefix="NADS", value=b"private-serial-A", rf="23:30:00:00:00:0a", gen=1,
          index=1, run="private-run", scenario="HIL-01", label="Tracker A", kind="ack", time=None):
    addr = bytes.fromhex(rf.replace(":", ""))
    ack = prefix.encode() + value
    wire = b"\x28\x00\x00" + addr + b"\x01\x01" + bytes([len(ack)]) + ack
    if kind != "ack":
        wire = b"\x18\x0c\x00\x00\x00" + bytes([kind == "unpair"]) + addr
    common = dict(capture_run_id=run, evidence_kind="synthetic", scenario=scenario,
        transport_generation=gen, observed_monotonic_ns=index * 1_000_000 if time is None else time,
        raw_report_index=index, raw_report_reference=f"private-reference:{run}:{index}")
    raw = dict(common, kind="raw_report", raw_report_hex=wire.hex())
    parsed = dict(common, kind=kind, rf_address=rf, slot=addr[1] & 15,
        candidate_namespace=prefix, candidate_value_hex=value.hex(), raw_ack_hex=ack.hex(),
        candidate_value=value.decode(errors="replace"), private_note="private-free-text")
    binding = dict(capture_run_id=run, scenario=scenario, rf_address=rf, transport_generation=gen,
        first_raw_report_index=index, last_raw_report_index=index,
        physical_tracker_label=label, physical_confirmation=True)
    return [raw, parsed], [binding]


def combine(*fixtures):
    return [r for records, _ in fixtures for r in records], [b for _, bindings in fixtures for b in bindings]


class OfflineTests(unittest.TestCase):
    def test_same_tracker_same_candidate_and_changed_contexts(self):
        records, bindings = combine(event(), event(rf="23:33:00:00:00:0b", gen=2, index=2))
        result = offline.analyze(records, bindings)
        row = result["candidates"]["NADS"]["trackers"]["Tracker A"]
        self.assertEqual(row, dict(observed_values=1, stability="OBSERVED_EQUAL", RF_count=2,
                                  slot_count=2, run_generation_count=2))
        self.assertFalse(result["failures"])

    def test_different_trackers(self):
        records, bindings = combine(event(), event(value=b"private-serial-B", rf="23:34:00:00:00:0b", index=2, label="Tracker B"))
        result = offline.analyze(records, bindings)
        self.assertFalse(result["failures"])
        self.assertNotEqual(result["timeline"][0]["candidate"], result["timeline"][1]["candidate"])

    def test_duplicate_live_each_namespace(self):
        for ns in offline.IDENTITIES:
            records, bindings = combine(event(prefix=ns), event(prefix=ns, rf="23:34:00:00:00:0b", index=2, label="Tracker B"))
            result = offline.analyze(records, bindings)
            self.assertIn("duplicate_live_candidate", result["failures"])
            self.assertEqual(result["status"], "FAIL_CLOSED")

    def test_mutation_across_run_generation_rf_and_slot(self):
        for ns in offline.IDENTITIES:
            records, bindings = combine(event(prefix=ns), event(prefix=ns, value=b"changed", rf="23:33:00:00:00:0b", gen=2, run="another-run", index=2))
            self.assertIn("physical_tracker_candidate_mutation", offline.analyze(records, bindings)["failures"])

    def test_missing_marker_does_not_promote(self):
        records, bindings = event(scenario=None)
        result = offline.analyze(records, bindings)
        self.assertEqual(result["status"], "INSUFFICIENT_EVIDENCE")
        self.assertIn("missing_or_unknown_scenario_marker", result["insufficient"])
        self.assertEqual(result["candidates"]["NADS"]["class"], "B_HIL_REQUIRED")

    def test_missing_or_ambiguous_binding(self):
        records, bindings = event()
        self.assertIn("missing_human_binding", offline.analyze(records, [])["insufficient"])
        self.assertIn("ambiguous_human_binding", offline.analyze(records, bindings * 2)["failures"])
        bindings[0]["transport_generation"] = 2
        self.assertIn("missing_human_binding", offline.analyze(records, bindings)["insufficient"])

    def test_malformed_ack_and_metadata(self):
        mutations = [("raw_ack_hex", "z"), ("raw_ack_hex", "00"),
                     ("candidate_value_hex", "00"), ("observed_monotonic_ns", -1),
                     ("slot", 4), ("rf_address", "invalid"), ("scenario", "HIL-05")]
        for field, value in mutations:
            records, bindings = event()
            records[-1][field] = value
            self.assertIn("malformed_or_unproven_event", offline.analyze(records, bindings)["failures"], field)
        records, bindings = event()
        records[0]["raw_report_hex"] = "28"
        self.assertIn("malformed_or_unproven_event", offline.analyze(records, bindings)["failures"])

    def test_missing_raw_and_duplicate_reference(self):
        records, bindings = event()
        self.assertIn("malformed_or_unproven_event", offline.analyze(records[1:], bindings)["failures"])
        self.assertIn("duplicate_raw_reference", offline.analyze([records[0]] + records, bindings)["failures"])

    def test_alias_byte_equality_across_namespaces_and_hex_case(self):
        records, bindings = combine(event(), event(prefix="NASS", index=2), event(prefix="NASS", value=b"other", index=3))
        records[3]["candidate_value_hex"] = records[3]["candidate_value_hex"].upper()
        timeline = offline.analyze(records, bindings)["timeline"]
        self.assertEqual(timeline[0]["equality_token"], timeline[1]["equality_token"])
        self.assertNotEqual(timeline[1]["equality_token"], timeline[2]["equality_token"])

    def test_public_allowlist_never_copies_identifiers_or_free_text(self):
        records, bindings = event(prefix="N" + "private-serial-A")
        result = json.dumps(offline.analyze(records, bindings))
        for secret in ("private-serial-A", "23:30:00:00:00:0a", "private-run", "private-reference", "private-free-text", records[0]["raw_report_hex"]):
            self.assertNotIn(secret, result)
        self.assertIn("N_OTHER", result)

    def test_sequence_longest_prefix_and_partial_burst(self):
        records, bindings = combine(event(prefix="NANA", kind="pair"),
            event(prefix="NAV1", index=2), event(prefix="NADS", index=3), event(prefix="NASS", index=4))
        result = offline.analyze(records, bindings)
        self.assertEqual([e["event"] for e in result["timeline"]], ["pair", "NAV1", "NADS", "NASS"])
        self.assertEqual([e["interval_ns"] for e in result["bursts"][0]["events"]], [0, 1_000_000, 1_000_000])
        self.assertFalse(result["bursts"][0]["missing_identity_namespaces"])
        records, bindings = event()
        self.assertEqual(offline.analyze(records, bindings)["bursts"][0]["missing_identity_namespaces"], ["NASS"])

    def test_generation_and_unpair_retire_duplicates(self):
        for boundary in ("unpair", "generation", "transport_lost", "pair", "slot"):
            a = event()
            extra = []
            if boundary in ("unpair", "pair"):
                extra.append(event(kind=boundary, index=2))
            elif boundary == "transport_lost":
                extra.append(([dict(capture_run_id="private-run", scenario="HIL-01", evidence_kind="synthetic", kind=boundary, transport_generation=1)], []))
            elif boundary == "slot":
                extra.append(event(kind="pair", rf="23:30:00:00:00:0b", index=2))
            b = event(rf="23:30:00:00:00:0b" if boundary == "slot" else "23:34:00:00:00:0b",
                      index=3, gen=2 if boundary == "generation" else 1)
            records, bindings = combine(a, *extra, b)
            self.assertNotIn("duplicate_live_candidate", offline.analyze(records, bindings)["failures"], boundary)

    def test_ack_cannot_silently_replace_live_rf(self):
        records, bindings = combine(event(), event(rf="23:30:00:00:00:0b", index=2))
        result = offline.analyze(records, bindings)
        self.assertIn("unproven_slot_replacement", result["failures"])
        self.assertIn("duplicate_live_candidate", result["failures"])

    def test_reordered_or_retired_evidence_fails(self):
        records, bindings = combine(event(index=2, gen=2), event(index=1, gen=1))
        self.assertIn("retired_generation_record", offline.analyze(records, bindings)["failures"])
        records, bindings = combine(event(index=2), event(index=1))
        self.assertIn("malformed_or_unproven_event", offline.analyze(records, bindings)["failures"])

    def test_complete_synthetic_never_class_a(self):
        fixtures = []
        for i, scenario in enumerate(offline.SCENARIOS):
            for j, label in enumerate(offline.LABELS):
                for k, ns in enumerate(offline.IDENTITIES):
                    fixtures.append(event(prefix=ns, value=label.encode(), rf=f"23:3{j}:00:00:00:0{j}",
                        scenario=scenario, run=f"run-{i}", index=1+j*2+k, label=label))
        records, bindings = combine(*fixtures)
        result = offline.analyze(records, bindings)
        self.assertEqual(result["status"], "OBSERVATIONS_ONLY")
        self.assertTrue(all(c["class"] == "B_HIL_REQUIRED" for c in result["candidates"].values()))
        self.assertEqual(result["hardware_checks_this_analysis"], "NOT_RUN")

    def test_cli_malformed_input_is_sanitized_and_nonzero(self):
        with tempfile.TemporaryDirectory() as root:
            capture, bindings = Path(root)/"capture.ndjson", Path(root)/"bindings.json"
            capture.write_text('{"raw_serial":"private-secret" invalid}', encoding="utf-8")
            bindings.write_text('{"bindings":[]}', encoding="utf-8")
            result = subprocess.run([sys.executable, str(Path(__file__).parents[1]/"scripts/analyze_identity_offline.py"),
                "--bindings", str(bindings), str(capture)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertNotIn("private-secret", result.stdout + result.stderr)
            self.assertEqual(json.loads(result.stdout)["status"], "FAIL_CLOSED")


if __name__ == "__main__":
    unittest.main()
