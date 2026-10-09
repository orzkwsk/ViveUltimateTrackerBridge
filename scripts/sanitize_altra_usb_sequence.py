"""Offline-only tshark CSV sanitizer. No device access or command transmission.

Only an allowlist of command labels survives. IRP IDs, addresses, payloads,
descriptor serials and absolute timestamps never enter the output.
"""
import argparse
import csv
import json
import pathlib
import re
from decimal import Decimal, InvalidOperation


TOKENS = ('NANA', 'NADS', 'NASS', 'NASI', 'NAPI', 'NAV1', 'NARI', 'NAGN', 'NAZZ')
ACKS = {'ARI1', 'ATM-1', 'ATH1', 'ATH0', 'AWH1', 'AWH0', 'ATM20', 'ATM11',
        'ATM21', 'FW1', 'FW2', 'NA'}
REQUIRED = {'frame.number', 'frame.time_epoch', 'usb.transfer_type',
            'usb.endpoint_address', 'usb.data_len', 'usb.bus_id', 'usb.device_address'}


def number(value, default=None):
    if value in (None, ''):
        return default
    if not isinstance(value, str) or not re.fullmatch(r'(?:0x[0-9a-fA-F]+|[0-9]+)', value):
        raise ValueError('Invalid numeric field')
    return int(value, 16 if value.startswith('0x') else 10)


def hex_bytes(value):
    if not value:
        return b''
    if not re.fullmatch(r'(?:[0-9a-fA-F]{2}:?)+', value):
        raise ValueError('Invalid hex field')
    return bytes.fromhex(value.replace(':', ''))


def label_ack(data, incoming=False):
    if incoming:
        for token in TOKENS:
            if data.startswith(token.encode('ascii')):
                return token
    try:
        text = data.decode('ascii')
    except UnicodeDecodeError:
        return 'unknown'
    if text in ACKS:
        return text
    if re.fullmatch(r'ANI[0-4]', text):
        return text
    if re.fullmatch(r'Wc[A-Z]{2}', text):
        return 'Wc<country>'
    return 'unknown'


def classify(data, direction):
    """Repository protocol framing only; unsupported variants remain unknown."""
    if len(data) >= 2 and data[0] == 0 and data[1] in (0x18, 0x1d, 0x1e, 0x28):
        data = data[1:]
    if not data:
        return 'empty'
    if direction == 'host-to-device':
        if len(data) < 2 or data[1] < 2 or data[1] > len(data):
            return 'malformed-vendor'
        payload = data[2:data[1]]
        if data[0] == 0x1d and len(payload) == 7:
            if payload[0] in (0, 1) and payload[1] in (0, 1) and all(x in (0, 1) for x in payload[2:]):
                name = 'PairDevice' if payload[0] == 0 else 'RxPowerSaving'
                return name + ('(on)' if payload[1] else '(off)')
        if data[0] == 0x18 and len(payload) >= 10 and payload[0] == 4:
            size = payload[9]
            if len(payload) == 10 + size:
                return label_ack(payload[10:])
        return 'unknown'
    if direction != 'device-to-host':
        return 'unknown'
    if data[0] == 0x1e:
        if len(data) >= 6 and 4 <= data[1] <= len(data):
            payload = data[4:data[1]]
            if len(payload) >= 2 and payload[1] >= 23 and len(payload) >= 2 + payload[1]:
                return 'RF status'
        return 'malformed-vendor'
    if data[0] == 0x18:
        if len(data) >= 12 and 12 <= data[1] <= len(data):
            return 'DeviceUnpaired event' if data[5] else 'DevicePaired event'
        return 'malformed-vendor'
    if data[0] == 0x28:
        if len(data) < 12 or len(data) < 12 + data[11]:
            return 'malformed-vendor'
        kind = int.from_bytes(data[9:11], 'little')
        payload = data[12:12 + data[11]]
        if kind == 0x0101:
            return label_ack(payload, incoming=True)
        if kind == 0x0110 and len(payload) in (37, 39):
            return 'pose'
    return 'unknown'


def sanitize(rows):
    events = []
    seen = {}
    irps = {}
    starts = {}
    frames = {}
    duplicates = 0
    previous_frame = 0
    previous_time = None
    origin = None
    target = None
    for row in rows:
        if not REQUIRED.issubset(row):
            raise ValueError('Missing required CSV columns')
        bus, device = number(row['usb.bus_id']), number(row['usb.device_address'])
        if bus is None or not 0 <= bus <= 65535 or device is None or not 1 <= device <= 127:
            raise ValueError('Invalid USB target')
        if target is None:
            target = (bus, device)
        if target != (bus, device):
            raise ValueError('Multiple devices: filter the CSV to the inventoried dongle')
        frame = number(row['frame.number'])
        try:
            timestamp = Decimal(row['frame.time_epoch'])
        except (InvalidOperation, TypeError):
            raise ValueError('Invalid timestamp') from None
        if not timestamp.is_finite() or timestamp < 0 or frame is None or frame <= 0:
            raise ValueError('Invalid frame/time')
        if frame in seen:
            if seen[frame] != row:
                raise ValueError('Conflicting duplicate frame')
            duplicates += 1
            continue
        if frame <= previous_frame or (previous_time is not None and timestamp < previous_time):
            raise ValueError('Out-of-order input')
        seen[frame] = dict(row)
        previous_frame, previous_time = frame, timestamp
        if origin is None:
            origin = timestamp
        transfer_type = number(row['usb.transfer_type'])
        endpoint = number(row['usb.endpoint_address'])
        length = number(row['usb.data_len'])
        stage = number(row.get('usb.control_stage'))
        if transfer_type is None or endpoint is None or endpoint > 255 or length is None:
            raise ValueError('Missing/invalid USB metadata')
        payloads = [hex_bytes(row.get(k, '')) for k in ('usbhid.data', 'usb.capdata', 'usbhid.data.vendor')]
        nonempty = [p for p in payloads if p]
        data = nonempty[0] if nonempty else b''
        # Never concatenate fields: dissectors may expose the same bytes twice.
        if len(data) > length:
            raise ValueError('Payload exceeds captured data length')
        irp = row.get('usb.irp_id', '')
        if irp and irp not in irps:
            irps[irp] = 'URB-' + str(len(irps) + 1)
        request = number(row.get('usbhid.setup.bRequest') or row.get('usb.setup.bRequest'))
        value = number(row.get('usbhid.setup.wValue') or row.get('usb.setup.wValue'))
        bm = number(row.get('usb.bmRequestType'))
        request_frame = number(row.get('usb.request_in'))
        context = frames.get(request_frame) if request_frame else starts.get(irp)
        if stage == 0:
            context = (request, value, bm)
            frames[frame] = context
            if irp:
                starts[irp] = context
        elif context:
            request, value, bm = context
        control = transfer_type == 2
        direction = ('device-to-host' if bm & 0x80 else 'host-to-device') if control and bm is not None else (
            'device-to-host' if endpoint & 0x80 else 'host-to-device') if not control else 'unknown'
        hid = control and bm is not None and (bm & 0x7f) == 0x21
        feature = hid and value is not None and value >> 8 == 3
        report_id = value & 255 if hid and value is not None else number(row.get('usbhid.data.report_id'))
        if report_id is not None and report_id > 255:
            raise ValueError('Invalid report ID')
        if feature and request == 9 and direction == 'host-to-device':
            category = 'HID SetFeature'
        elif feature and request == 1 and direction == 'device-to-host':
            category = 'HID GetFeature'
        elif hid:
            category = 'HID control (other)'
        elif control:
            category = 'USB control'
        elif transfer_type == 1:
            category = 'USB interrupt'
        else:
            category = 'unknown transfer'
        # Descriptor and standard-control data cannot be vendor command evidence.
        vendor = classify(data, direction) if data and (feature or transfer_type == 1) else 'not classified'
        event = {'frame': frame, 'relative_s': float(timestamp - origin), 'bus_alias': 'DONGLE-BUS',
                 'device_alias': 'DONGLE', 'urb_alias': irps.get(irp), 'direction': direction,
                 'transfer_type': transfer_type, 'category': category, 'control_stage': stage,
                 'report_id': report_id, 'captured_data_length': length,
                 'request_length': number(row.get('usb.setup.wLength')), 'command': vendor,
                 'usb_status': number(row.get('usb.usbd_status'))}
        events.append(event)
        if stage == 3 and irp:
            starts.pop(irp, None)
    return {'schema': 'altra-usb-sequence-research-1', 'io': 'offline CSV only',
            'duplicates_removed': duplicates, 'events': events,
            'privacy': 'No payload, serial, RF, raw IRP, topology or absolute timestamp fields.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', required=True, type=pathlib.Path)
    parser.add_argument('--output', required=True, type=pathlib.Path)
    args = parser.parse_args()
    try:
        with args.input.open(encoding='utf-8-sig', newline='') as stream:
            result = sanitize(csv.DictReader(stream))
    except ValueError:
        parser.exit(2, 'Invalid pcap-derived input; no sanitized output written.\n')
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main()
