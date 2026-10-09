"""Offline research decoding, with no device APIs or transmit path.

Consumes filtered tshark CSV, exports only allowlisted structural metadata.
RE command names are deliberately not treated as vendor semantics or safety.
"""
import argparse
import collections
import csv
import json
import pathlib
import re

import sanitize_altra_usb_sequence as usb


def decode_host(data):
    """Recognize wire structure; never return destination/selector/arguments."""
    if len(data) == 65 and data[0] == 0:
        data = data[1:]
    if not data:
        return {'form': 'empty'}
    if data[:2] == b'\x1e\x01' and len(data) == 64 and not any(data[2:]):
        return {'form': 'host-query-1e01', 'command': '0x1e query variant',
                'vendor_semantics': 'UNKNOWN'}
    if len(data) < 2 or not 2 <= data[1] <= len(data) or any(data[data[1]:]):
        return {'form': 'malformed-or-unrecognized-padding'}
    payload = data[2:data[1]]
    if data[0] == 0x1d:
        if (len(payload) != 7 or payload[1] not in (0, 1)
                or any(x not in (0, 1) for x in payload[2:])):
            return {'form': 'malformed-rf-behavior'}
        names = {0: 'PairDevice', 1: 'RxPowerSaving'}
        behavior = payload[0]
        return {'form': 'rf-behavior', 'behavior': behavior, 'flag': payload[1],
                'selected_position_count': sum(payload[2:]),
                'command': names.get(behavior, 'RF behavior ' + str(behavior)),
                'vendor_semantics': 'RE label only' if behavior in names else 'UNKNOWN'}
    if data[0] == 0x18:
        if (len(payload) < 10 or payload[0] not in (3, 4)
                or payload[7:9] != b'\x00\x01' or len(payload) != 10 + payload[9]):
            return {'form': 'malformed-or-unknown-tx'}
        label = usb.label_ack(payload[10:])
        if re.fullmatch(r'ANI[0-4]', label):
            label = 'ANI<slot>'
        return {'form': 'tx-ascii', 'payload_kind': '0x%02x' % payload[0],
                'destination_width_bytes': 6, 'controls': [0, 1],
                'ascii_length': payload[9], 'command': label,
                'address_matching_semantics': 'UNKNOWN'}
    return {'form': 'unknown-setfeature', 'command_id': '0x%02x' % data[0]}


def reconcile(rows):
    rows = list(rows)
    sanitized = usb.sanitize(rows)
    by_frame = {usb.number(r['frame.number']): r for r in rows}
    events = sanitized['events']
    requests = {}
    commands = []
    for event in events:
        if event['category'] == 'HID SetFeature' and event['control_stage'] == 0:
            metadata = decode_host(usb.payload_bytes(by_frame[event['frame']]))
            item = {'frame': event['frame'], 'relative_s': event['relative_s'],
                    'report_bytes': len(usb.payload_bytes(by_frame[event['frame']])),
                    'request_length': event['request_length'], 'wire': metadata,
                    'usb_completion': None}
            commands.append(item)
            requests[event['frame']] = item
        elif event['control_stage'] == 3:
            request = usb.number(by_frame[event['frame']].get('usb.request_in'))
            if request in requests:
                if requests[request]['usb_completion'] is not None:
                    raise ValueError('Multiple completions for SetFeature')
                requests[request]['usb_completion'] = {
                    'frame': event['frame'], 'relative_s': event['relative_s'],
                    'status': event['usb_status'],
                    'meaning': 'USB transfer result only; not semantic vendor ACK'}
    # Preserve every distinct transfer and incoming ordering. Never deduplicate
    # identical payloads from different frames.
    incoming = [{'frame': e['frame'], 'relative_s': e['relative_s'], 'command': e['command']}
                for e in events if e['direction'] == 'device-to-host'
                and e['command'] not in ('pose', 'RF status', 'unknown', 'empty',
                                         'not classified', 'malformed-vendor')]
    poses = [e for e in events if e['command'] == 'pose']
    counts = collections.Counter(c['wire']['form'] for c in commands)
    return {'schema': 'altra-offline-wire-reconciliation-1', 'hardware_io': 'NONE',
            'setfeature': commands, 'incoming_order': incoming,
            'pose_report_count': len(poses), 'first_pose':
            ({'frame': poses[0]['frame'], 'relative_s': poses[0]['relative_s']} if poses else None),
            'form_counts': dict(counts),
            'privacy': 'No raw serial/RF, selector positions, country, epoch, topology or payload.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    args = parser.parse_args()
    try:
        with args.input.open(encoding='utf-8-sig', newline='') as stream:
            result = reconcile(csv.DictReader(stream))
    except ValueError:
        parser.exit(2, 'Invalid capture-derived input; no output written.\n')
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main()
