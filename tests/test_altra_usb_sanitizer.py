import importlib.util
import json
import pathlib
import unittest

PATH = pathlib.Path(__file__).resolve().parents[1] / 'scripts' / 'sanitize_altra_usb_sequence.py'
spec = importlib.util.spec_from_file_location('sanitizer', PATH)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def row(frame=1, payload='', **fields):
    data = {'frame.number': str(frame), 'frame.time_epoch': str(1000 + frame),
            'usb.bus_id': '3', 'usb.device_address': '1',
            'usb.transfer_type': '0x01', 'usb.endpoint_address': '0x81',
            'usb.data_len': str(len(bytes.fromhex(payload))), 'usbhid.data': payload}
    data.update(fields)
    return data


def outgoing(text):
    payload = bytes([4]) + bytes.fromhex('aabbccddeeff') + bytes([0, 1, len(text)]) + text.encode()
    return bytes([0x18, len(payload) + 2]) + payload


def incoming(text):
    payload = text.encode()
    return bytes([0x28, 1, 0]) + bytes.fromhex('aabbccddeeff') + bytes([1, 1, len(payload)]) + payload


class SanitizerTest(unittest.TestCase):
    def test_malformed_numeric_time_hex_missing_length(self):
        for field, value in [('frame.number', 'secret'), ('frame.time_epoch', 'NaN'),
                             ('usbhid.data', 'nothex'), ('usb.data_len', '')]:
            with self.subTest(field=field), self.assertRaises(ValueError):
                module.sanitize([row(**{field: value})])
        with self.assertRaises(ValueError):
            module.sanitize([{}])
        with self.assertRaises(ValueError):
            module.sanitize([row(payload='1e', **{'usb.data_len': '0'})])

    def test_unknown_transfer_and_payload_redaction(self):
        event = module.sanitize([row(payload='cafe', **{'usb.transfer_type': '0x03'})])['events'][0]
        self.assertEqual(event['category'], 'unknown transfer')
        self.assertEqual(event['command'], 'not classified')
        self.assertNotIn('cafe', json.dumps(event))

    def test_known_transmit_and_incoming_commands(self):
        for text in module.ACKS | {'ANI0', 'ANI4'}:
            with self.subTest(text=text):
                wire = outgoing(text)
                self.assertEqual(module.classify(wire, 'host-to-device'), text)
                self.assertEqual(module.classify(b'\x00' + wire, 'host-to-device'), text)
        self.assertEqual(module.classify(outgoing('WcJP'), 'host-to-device'), 'Wc<country>')
        for name in module.TOKENS:
            self.assertEqual(module.classify(incoming(name + 'PRIVATE-SERIAL'), 'device-to-host'), name)
        self.assertEqual(module.classify(outgoing('PRIVATE-SERIAL'), 'host-to-device'), 'unknown')
        self.assertEqual(module.classify(b'\x1d\x09\x01\x00\x01\x00\x00\x00\x00', 'host-to-device'), 'RxPowerSaving(off)')
        self.assertEqual(module.classify(b'\x1d\x09\x00\x01\x01\x00\x00\x00\x00', 'host-to-device'), 'PairDevice(on)')

    def test_set_getfeature_not_standard_set_configuration(self):
        base = {'usb.transfer_type': '0x02', 'usb.control_stage': '0',
                'usb.bmRequestType': '0x21', 'usbhid.setup.bRequest': '0x09',
                'usbhid.setup.wValue': '0x0300', 'usb.irp_id': '0xprivate'}
        start = row(payload=outgoing('NA').hex(), **base)
        result = module.sanitize([start])['events'][0]
        self.assertEqual((result['category'], result['command'], result['report_id']), ('HID SetFeature', 'NA', 0))
        standard = dict(start, **{'usb.bmRequestType': '0x00'})
        self.assertEqual(module.sanitize([standard])['events'][0]['command'], 'not classified')
        get = dict(start, **{'usb.bmRequestType': '0xa1', 'usbhid.setup.bRequest': '0x01',
                            'usbhid.data': '', 'usb.data_len': '8'})
        self.assertEqual(module.sanitize([get])['events'][0]['category'], 'HID GetFeature')

    def test_order_and_duplicates_preserve_distinct_transfers(self):
        a, b = row(1), row(2)
        result = module.sanitize([a, dict(a), b])
        self.assertEqual([x['frame'] for x in result['events']], [1, 2])
        self.assertEqual(result['duplicates_removed'], 1)
        with self.assertRaises(ValueError):
            module.sanitize([b, a])
        with self.assertRaises(ValueError):
            module.sanitize([a, dict(a, **{'usb.data_len': '3'})])

    def test_completion_correlates_without_duplicate_command(self):
        start = row(**{'usb.transfer_type': '0x02', 'usb.control_stage': '0',
                       'usb.bmRequestType': '0x21', 'usbhid.setup.bRequest': '0x09',
                       'usbhid.setup.wValue': '0x0300', 'usb.irp_id': '0xdeadbeef',
                       'usbhid.data': outgoing('FW1').hex(), 'usb.data_len': str(len(outgoing('FW1')))})
        end = row(2, **{'usb.transfer_type': '0x02', 'usb.control_stage': '3',
                        'usb.request_in': '1', 'usb.irp_id': '0xdeadbeef'})
        events = module.sanitize([start, end])['events']
        self.assertEqual([e['command'] for e in events], ['FW1', 'not classified'])
        self.assertEqual(events[0]['urb_alias'], events[1]['urb_alias'])
        self.assertNotIn('deadbeef', json.dumps(events))

    def test_identity_and_unknown_payload_never_survive(self):
        secret = 'SYNTHETIC-PRIVATE-SERIAL'
        wire = incoming('NADS' + secret).hex()
        result = module.sanitize([row(payload=wire, **{'usb.device_address': '123',
                                  'usb.bus_id': '42', 'usb.irp_id': secret,
                                  'usb.iSerialNumber': secret, 'irrelevant': secret})])
        public = json.dumps(result)
        for forbidden in (secret, secret.encode().hex(), 'aabbccddeeff', wire, '1001'):
            self.assertNotIn(forbidden, public)
        self.assertEqual(result['events'][0]['command'], 'NADS')

    def test_multiple_usb_targets_rejected(self):
        with self.assertRaises(ValueError):
            module.sanitize([row(1), row(2, **{'usb.device_address': '2'})])


if __name__ == '__main__':
    unittest.main()
