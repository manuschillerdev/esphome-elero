"""Follow-up: known USB operations only; paired receivers are unavailable.

This script can cause RF transmissions. It does not program or erase devices.
"""
import json
import time
from pathlib import Path

import serial

ROOT = Path('/tmp/elero-official-api')
output = (ROOT / 'isolation-session.jsonl').open('w', buffering=1)
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


try:
    exchange('initial_passive_window', [], 8.25)
    version = query('baseline_version', packet(0x41), .6)
    channels = query('baseline_channels', packet(0x4a), .6)
    if version[4:9] != b'01.10' or channels != bytes.fromhex('aa044b000304'):
        raise RuntimeError('Identity changed')
    query('valid_native_info_1_before_bad_2', native_info(1))
    bad = bytearray(native_info(2)); bad[-1] ^= 1
    query('bad_checksum_native_info_2_after_1', bytes(bad))
    exchange('passive_after_bad_native', [], 4.25)
    query('valid_native_info_2_before_bad_1', native_info(2))
    bad = bytearray(native_info(1)); bad[-1] ^= 1
    query('bad_checksum_native_info_1_after_2', bytes(bad))
    query('zero_native_after_2', native_info(0))
    query('valid_native_info_1_before_zero', native_info(1))
    query('zero_native_after_1', native_info(0))
    query('valid_easy_info_1', packet(0x4e, b'\x00\x01'))
    bad = bytearray(packet(0x4e, b'\x00\x02')); bad[-1] ^= 1
    query('bad_checksum_easy_info_2_after_1', bytes(bad))
    query('recovery_check', packet(0x4a), .6)
    query('native_group_0003_14_second_window', native_info(3), 14.25)
    query('easy_group_0003_14_second_window', packet(0x4e, b'\x00\x03'), 14.25)
    query('final_version', packet(0x41), .6)
    query('final_channels', packet(0x4a), .6)
    exchange('final_passive_window', [], 2.25)
finally:
    serial_port.close()
    output.close()
