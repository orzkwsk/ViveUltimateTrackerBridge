"""Synthetic comparison checks. These are never HIL evidence."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("compare", Path(__file__).parents[1] / "scripts/compare_identity_hil.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def fixture(scenario="HIL-01", rf="23:30:00:00:00:0a", value="synthetic-A", ns="NADS", gen=1, index=1):
    address = bytes.fromhex(rf.replace(":", ""))
    slot = address[1] & 15
    ack_bytes = (ns + value).encode()
    wire = b"\x28\x00\x00" + address + b"\x01\x01" + bytes([len(ack_bytes)]) + ack_bytes
    common = dict(capture_run_id="synthetic-run", evidence_kind="synthetic", scenario=scenario,
                  rf_address=rf, slot=slot, transport_generation=gen, observed_monotonic_ns=index,
                  raw_report_index=index, raw_report_reference=f"raw:{index}")
    raw = dict(common, kind="raw_report", raw_report_hex=wire.hex())
    ack = dict(common, kind="ack", candidate_namespace=ns, candidate_value_hex=value.encode().hex(),
               raw_ack_hex=(ns+value).encode().hex())
    binding = dict(capture_run_id="synthetic-run", scenario=scenario, rf_address=rf, transport_generation=gen,
                   first_raw_report_index=index, last_raw_report_index=index, physical_tracker_label="Tracker A", physical_confirmation=True)
    return [raw, ack], binding


class CompareTests(unittest.TestCase):
    def test_missing_and_unconfirmed(self):
        records, _ = fixture()
        result = module.compare(records, [])
        self.assertIn("unconfirmed_physical_label", result["issues"])
        self.assertEqual(result["candidates"]["NADS"]["class"], "B_HIL_REQUIRED")

    def test_conflicting_same_rf_and_length(self):
        a, ba = fixture()
        b, bb = fixture(value="different-value", index=2)
        issues = module.compare(a+b, [ba, bb])["issues"]
        self.assertIn("multiple_values_same_rf_namespace", issues)
        self.assertIn("observed_length_changed", issues)

    def test_duplicate_live_and_namespace_independence(self):
        a, ba = fixture()
        b, bb = fixture(rf="23:31:00:00:00:0b", index=2)
        bb["physical_tracker_label"] = "Tracker B"
        self.assertIn("duplicate_live_candidate", module.compare(a+b, [ba, bb])["issues"])
        for value in ("synthetic-A", "synthetic-different"):
            c, bc = fixture(ns="NASS", value=value, index=3)
            self.assertFalse(module.compare(a+c, [ba, bc])["issues"])

    def test_stale_binding_and_bad_parser(self):
        a, ba = fixture(gen=2)
        ba["transport_generation"] = 1
        self.assertIn("unconfirmed_physical_label", module.compare(a, [ba])["issues"])
        a[-1]["raw_ack_hex"] = "00"
        self.assertIn("parser_raw_mismatch", module.compare(a, [ba])["issues"])

    def test_raw_reference_and_empty(self):
        a, ba = fixture(value="")
        self.assertIn("empty_or_nonprintable_candidate", module.compare(a, [ba])["issues"])
        self.assertIn("missing_or_mismatched_raw_evidence", module.compare(a[1:], [ba])["issues"])

    def test_bad_binding(self):
        a, ba = fixture()
        ba["physical_confirmation"] = False
        with self.assertRaises(ValueError): module.compare(a, [ba])

    def test_connection_order_requires_confirmed_pair_events(self):
        records, bindings, index = [], [], 0
        for scenario, labels in (("HIL-01", ("Tracker A", "Tracker B")),
                                 ("HIL-05", ("Tracker B", "Tracker A"))):
            for label in labels:
                index += 2
                rf = "23:30:00:00:00:0a" if label == "Tracker A" else "23:34:00:00:00:0b"
                events, binding = fixture(scenario=scenario, rf=rf, value=label, index=index)
                binding["physical_tracker_label"] = label
                binding["first_raw_report_index"] = index - 1
                pair = dict(events[-1], kind="pair", raw_report_index=index-1)
                records += [pair] + events
                bindings.append(binding)
        outcome = module.compare(records, bindings)["candidates"]["NADS"]
        self.assertTrue(outcome["connection_order_changed"])
        self.assertFalse(any(outcome["slot_change_observed"].values()))
        without_pairs = [r for r in records if r["kind"] != "pair"]
        self.assertFalse(module.compare(without_pairs, bindings)["candidates"]["NADS"]["connection_order_changed"])
        self.assertEqual(outcome["class"], "B_HIL_REQUIRED")

    def test_complete_synthetic_never_promoted(self):
        records, bindings, index = [], [], 0
        for scenario in module.REQUIRED:
            for label in ("Tracker A", "Tracker B"):
                for ns in module.NAMES:
                    index += 1
                    slot = (0 if label == "Tracker A" else 1) ^ (1 if scenario == "HIL-05" else 0)
                    suffix = (10 if label == "Tracker A" else 11) + (16 if scenario == "HIL-06-after" else 0)
                    rf = f"23:3{slot}:00:00:00:{suffix:02x}"
                    a, b = fixture(scenario=scenario, rf=rf, value=label, ns=ns, index=index)
                    b["physical_tracker_label"] = label
                    records += a; bindings.append(b)
        result = module.compare(records, bindings)
        for ns in module.NAMES:
            self.assertTrue(result["candidates"][ns]["stable"])
            self.assertTrue(result["candidates"][ns]["A_B_unique"])
            self.assertFalse(result["candidates"][ns]["hardware_evidence"])
            self.assertEqual(result["candidates"][ns]["class"], "B_HIL_REQUIRED")


if __name__ == "__main__": unittest.main()
