"""Follow-up: known USB operations only; paired receivers are unavailable.

This script can cause RF transmissions. It does not program or erase devices.
"""
import json
import time
from pathlib import Path

import serial

ROOT = Path('/tmp/elero-official-api')
output = (ROOT / 'followup-session.jsonl').open('w', buffering=1)
serial_port = serial.Serial(port=None, baudrate=38400, timeout=.05, write_timeout=1, exclusive=True)
serial_port.dtr = False
serial_port.rts = False
serial_port.port = '/dev/serial/by-id/usb-elero_GmbH_Transmitter_Stick_A601IUA7-if00-port0'
serial_port.open()
start = time.monotonic()


def packet(opcode, data=b''):
    body = bytes([0xaa, len(data) + 2, opcode]) + data
    return body + bytes([-sum(body) & 255])


def native_info(mask):
    return packet(0x45, mask.to_bytes(2, 'big') + bytes.fromhex('0204000000000001'))


def native_key(mask, key, parameter):
    return packet(0x45, mask.to_bytes(2, 'big') + bytes([0, 4, key, 0, 0, parameter, 0, 1]))


def exchange(label, requests, timeout=4.25):
    row = {'label': label, 'tx': [], 'chunks': []}
    for delay, data in requests:
        if delay:
            time.sleep(delay)
        serial_port.write(data)
        serial_port.flush()
        row['tx'].append({'t': round(time.monotonic() - start, 4), 'hex': data.hex()})
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        data = serial_port.read(512)
        if data:
            row['chunks'].append({'t': round(time.monotonic() - start, 4), 'hex': data.hex()})
    output.write(json.dumps(row) + '\n')
    print(json.dumps(row), flush=True)
    return b''.join(bytes.fromhex(c['hex']) for c in row['chunks'])


def query(label, data, timeout=4.25):
    return exchange(label, [(0, data)], timeout)


identified = False
try:
    version = query('baseline_version', packet(0x41), .6)
    channels = query('baseline_channels', packet(0x4a), .6)
    if version[4:9] != b'01.10' or channels != bytes.fromhex('aa044b000304'):
        raise RuntimeError('Device identity or pairing changed; follow-up matrix was not started.')
    identified = True
    for mask in [6, 7, 0x0101, 0x0102, 0x0103, 0x4001, 0x4002, 0x4003]:
        query(f'easy_info_mask_{mask:04x}', packet(0x4e, mask.to_bytes(2, 'big')))
        query(f'native_info_mask_{mask:04x}', native_info(mask))
    query('native_channel_2_before_zero', native_info(2))
    query('native_zero_after_channel_2', native_info(0))
    query('easy_group_0003_long_window', packet(0x4e, b'\x00\x03'), 8.25)
    query('native_group_0003_long_window', native_info(3), 8.25)
    for mask in [1, 2, 3]:
        for key in [0x21, 0x41]:
            # Experimental key; parameter 1 is held equal to the tool's normal UP/DOWN path.
            query(f'experimental_native_mask_{mask}_key_{key:02x}', native_key(mask, key, 1))
            query(f'cleanup_native_mask_{mask}_key_{key:02x}', native_key(mask, 0x10, 0))
    exchange('native_interruption_1_then_2', [(0, native_info(1)), (.15, native_info(2))], 4.3)
    invalid = bytearray(native_info(1))
    invalid[-1] ^= 1
    query('native_info_bad_checksum', bytes(invalid))
    query('recovery_version', packet(0x41), .6)
    query('final_version', packet(0x41), .6)
    query('final_channels', packet(0x4a), .6)
finally:
    try:
        if identified:
            query('final_native_stop_channel_1', native_key(1, 0x10, 0))
            query('final_native_stop_channel_2', native_key(2, 0x10, 0))
    finally:
        serial_port.close()
        output.close()
