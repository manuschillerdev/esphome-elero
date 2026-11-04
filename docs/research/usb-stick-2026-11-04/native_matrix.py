"""Probe native USB commands reconstructed from the official application's IL."""
import json
import time
from pathlib import Path

import serial

root = Path('/tmp/elero-official-api')
log = (root / 'native-session.jsonl').open('w', buffering=1)
port = serial.Serial(port=None, baudrate=38400, timeout=.05, write_timeout=1, exclusive=True)
port.dtr = False
port.rts = False
port.port = '/dev/serial/by-id/usb-elero_GmbH_Transmitter_Stick_A601IUA7-if00-port0'
port.open()
start = time.monotonic()


def query(label, opcode, payload=b'', timeout=4.25):
    data = bytes([0xaa, len(payload) + 2, opcode]) + payload
    data += bytes([-sum(data) & 255])
    row = {'label': label, 't': round(time.monotonic() - start, 4), 'tx': data.hex(), 'chunks': []}
    port.write(data)
    port.flush()
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        chunk = port.read(512)
        if chunk:
            row['chunks'].append({'t': round(time.monotonic() - start, 4), 'hex': chunk.hex()})
    log.write(json.dumps(row) + '\n')
    print(json.dumps(row), flush=True)


def native(mask, key=0, duration=0, flag=1, info=False):
    # Exact SendInfo/SendDrive payloads in the official application.
    if info:
        return mask.to_bytes(2, 'big') + bytes([2, 4, 0, 0, 0, 0, 0, 1])
    return mask.to_bytes(2, 'big') + bytes([0, 4, key, duration, 0, flag, 0, 1])


try:
    query('native_baseline_version', 0x41, timeout=.6)
    query('native_read_clock', 0x44, timeout=.6)
    for mask in [1, 2, 3, 4, 5, 0, 0x7fff]:
        query(f'native_info_mask_{mask}', 0x45, native(mask, info=True))
    for mask in [1, 2, 3]:
        for name, key, flag in [('stop', 0x10, 0), ('up', 0x20, 1), ('down', 0x40, 1),
                                ('tilt', 0x24, 0), ('intermediate', 0x44, 4)]:
            query(f'native_mask_{mask}_{name}', 0x45, native(mask, key, flag=flag))
            if key != 0x10:
                query(f'native_cleanup_{mask}_{name}', 0x45, native(mask, 0x10, flag=0))
    query('native_final_version', 0x41, timeout=.6)
    query('native_final_channels', 0x4a, timeout=.6)
finally:
    try:
        query('native_final_stop_1', 0x45, native(1, 0x10, flag=0))
        query('native_final_stop_2', 0x45, native(2, 0x10, flag=0))
    finally:
        port.close()
        log.close()
