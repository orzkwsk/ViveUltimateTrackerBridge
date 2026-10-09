import json
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'scripts'))
import reconcile_altra_wire as research
import sanitize_altra_usb_sequence as usb


def tx(text, kind=3):
    # Synthetic destination only. Never a recorded hardware identifier.
    body = bytes([kind]) + bytes.fromhex('aabbccddeeff') + bytes([0, 1, len(text)]) + text.encode()
    return (bytes([0x18, len(body) + 2]) + body).ljust(64, b'\x00')


def row(frame, wire=b'', **fields):
    data = {'frame.number': str(frame), 'frame.time_epoch': str(1000 + frame),
            'usb.bus_id': '42', 'usb.device_address': '123', 'usb.irp_id': 'PRIVATE',
            'usb.transfer_type': '2', 'usb.endpoint_address': '0', 'usb.control_stage': '0',
            'usb.bmRequestType': '0x21', 'usbhid.setup.bRequest': '9',
            'usbhid.setup.wValue': '0x0300', 'usbhid.setup.wLength': '64',
            'usb.data_len': str(len(wire) + 8), 'usb.data_fragment': wire.hex()}
    data.update(fields)
    return data


class ReconciliationTest(unittest.TestCase):
    def test_full_and_partial_mac_kind_are_structural_not_semantic(self):
        for kind in (3, 4):
            item = research.decode_host(tx('FW2', kind))
            self.assertEqual(item['payload_kind'], f'0x{kind:02x}')
            self.assertEqual(item['address_matching_semantics'], 'UNKNOWN')
            self.assertEqual(usb.classify(tx('FW2', kind), 'host-to-device'), 'FW2')
        self.assertEqual(research.decode_host(tx('FW2', 5))['form'], 'malformed-or-unknown-tx')

    def test_behavior_2_6_does_not_export_selectors_or_assign_safety(self):
        for behavior in (2, 6):
            wire = bytes([0x1d, 9, behavior, 1, 0, 0, 1, 0, 0]).ljust(64, b'\x00')
            item = research.decode_host(wire)
            self.assertEqual(item['behavior'], behavior)
            self.assertEqual(item['selected_position_count'], 1)
            self.assertEqual(item['vendor_semantics'], 'UNKNOWN')
            self.assertNotIn('selector', json.dumps(item))

    def test_exact_query_variant_not_generic_short_packet(self):
        query = b'\x1e\x01' + bytes(62)
        self.assertEqual(research.decode_host(query)['form'], 'host-query-1e01')
        self.assertEqual(usb.classify(query, 'host-to-device'), '0x1e query variant')
        for bad in (query[:-1], b'\x1e\x01', query[:-1] + b'\x01'):
            self.assertEqual(research.decode_host(bad)['form'], 'malformed-or-unrecognized-padding')

    def test_allowlist_and_argument_redaction(self):
        cases = {'ANI4': 'ANI<slot>', 'WcZZ': 'Wc<country>', 'FW2': 'FW2',
                 'ATM21': 'ATM21', 'ATW': 'ATW', 'ACF60': 'ACF<number>',
                 'ATS1234567890': 'ATS<epoch>', 'pAS_ZZ123': 'pAS<parameters>',
                 'PRIVATE-SERIAL': 'unknown'}
        for text, expected in cases.items():
            self.assertEqual(research.decode_host(tx(text))['command'], expected)
        public = json.dumps(research.reconcile([row(i, tx(text))
                            for i, text in enumerate(cases, 1)]))
        for secret in ('1234567890', 'aabbccddeeff', 'ANI4', 'WcZZ', 'PRIVATE-SERIAL',
                       'pAS_ZZ', 'PRIVATE', '1001'):
            self.assertNotIn(secret, public)

    def test_unknown_malformed_and_conflicting_fields(self):
        self.assertEqual(research.decode_host(b'\xab\x02' + bytes(62))['form'], 'unknown-setfeature')
        for wire in (tx('FW2')[:12], bytes([0x18, 100]), tx('FW2')[:-1] + b'\x01'):
            self.assertEqual(research.decode_host(wire)['form'], 'malformed-or-unrecognized-padding')
        bad = bytearray(tx('FW2')); bad[10] = 2
        self.assertEqual(research.decode_host(bytes(bad))['form'], 'malformed-or-unknown-tx')
        with self.assertRaises(ValueError):
            research.reconcile([row(1, tx('FW2'), **{'usbhid.data': tx('FW1').hex()})])

    def test_order_completion_and_distinct_repeats(self):
        start = row(1, tx('FW2'))
        end = row(2, **{'usb.control_stage': '3', 'usb.request_in': '1',
                       'usb.data_len': '0', 'usb.usbd_status': '0'})
        second = row(3, tx('FW2'))
        result = research.reconcile([start, end, second])
        self.assertEqual([x['frame'] for x in result['setfeature']], [1, 3])
        self.assertEqual(result['setfeature'][0]['usb_completion']['frame'], 2)
        self.assertIsNone(result['setfeature'][1]['usb_completion'])
        self.assertEqual(result['setfeature'][0]['request_length'], 64)
        with self.assertRaises(ValueError):
            research.reconcile([second, start])

    def test_reviewed_24_frame_anonymous_fixture(self):
        path = pathlib.Path(__file__).resolve().parents[1] / 'docs/hil/altra-official-runtime-usb-sequence.json'
        captures = json.loads(path.read_text(encoding='utf-8'))['official_wire_backend_reconciliation']['captures']
        total = 0
        for capture in captures.values():
            fixture = capture['anonymous_command_fixture']['commands']
            self.assertEqual([f['frame'] for f in fixture], [c['frame'] for c in capture['setfeature']])
            for sample, command in zip(fixture, capture['setfeature']):
                data = bytes.fromhex(sample['wire_hex'])
                self.assertEqual(len(data), 64)
                decoded = research.decode_host(data)
                self.assertEqual(decoded['form'], command['wire']['form'])
                self.assertEqual(decoded['command'], sample['command'])
                if decoded['form'] == 'tx-ascii':
                    self.assertEqual(data[3:9], bytes.fromhex('aab0ccddeeff'))
                    self.assertEqual(data[1], 12 + decoded['ascii_length'])
                    self.assertEqual(decoded['payload_kind'], '0x03')
                    changed = bytearray(data); changed[2] = 4
                    self.assertEqual(research.decode_host(changed)['command'], sample['command'])
                total += 1
        self.assertEqual(total, 24)


if __name__ == '__main__':
    unittest.main()
