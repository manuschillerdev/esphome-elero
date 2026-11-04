import json
import signal
import time
from pathlib import Path

import serial

ROOT = Path('/tmp/elero-official-api')
LOG = ROOT / 'matrix-session.jsonl'
PORT = '/dev/serial/by-id/usb-elero_GmbH_Transmitter_Stick_A601IUA7-if00-port0'
start = time.monotonic()
stream = LOG.open('w', buffering=1)
s = serial.Serial(port=None, baudrate=38400, timeout=0.05, write_timeout=1, exclusive=True)
s.dtr = False
s.rts = False
s.port = PORT
s.open()
buffer = bytearray()


def log(kind, **data):
    row = {'t': round(time.monotonic() - start, 4), 'kind': kind, **data}
    stream.write(json.dumps(row) + '\n')
    return row


def frame(opcode, payload=b''):
    data = bytes([0xaa, len(payload) + 2, opcode]) + payload
    return data + bytes([-sum(data) & 0xff])


def send(data, label):
    s.write(data)
    s.flush()
    log('tx', label=label, hex=data.hex())


def receive(duration):
    deadline = time.monotonic() + duration
    found = []
    while time.monotonic() < deadline:
        data = s.read(512)
        if not data:
            continue
        log('rx_chunk', hex=data.hex())
        buffer.extend(data)
        while buffer:
            if buffer[0] != 0xaa:
                log('stray_byte', value=buffer.pop(0))
                continue
            if len(buffer) < 2:
                break
            length = buffer[1] + 2
            if buffer[1] == 0:
                log('unexpected_zero_length', hex=buffer.hex())
                buffer.clear()
                break
            if len(buffer) < length:
                break
            packet = bytes(buffer[:length])
            del buffer[:length]
            row = log('rx_frame', hex=packet.hex(), checksum_valid=sum(packet) % 256 == 0)
            found.append(row)
    return found


def query(label, opcode, payload=b'', timeout=4.25):
    data = frame(opcode, payload)
    tx_time = time.monotonic()
    send(data, label)
    replies = receive(timeout)
    result = log('case', label=label, tx=data.hex(), replies=[r['hex'] for r in replies],
                 first_reply_seconds=round(replies[0]['t'] - (tx_time - start), 3) if replies else None)
    print(json.dumps(result), flush=True)


def abort(signum, frame_):
    raise KeyboardInterrupt


signal.signal(signal.SIGTERM, abort)
try:
    query('baseline_version', 0x41, timeout=.6)
    query('baseline_channels', 0x4a, timeout=.6)
    for channel in range(1, 16):
        query(f'info_single_{channel}', 0x4e, (1 << (channel - 1)).to_bytes(2, 'big'))
    for name, mask in [('empty', 0), ('paired_group', 3), ('mixed_paired_unpaired', 5), ('all_15', 0x7fff)]:
        query('info_' + name, 0x4e, mask.to_bytes(2, 'big'))
    bad = bytearray(frame(0x4a))
    bad[-1] ^= 1
    send(bytes(bad), 'bad_checksum_check')
    replies = receive(1.1)
    log('case', label='bad_checksum_check', tx=bytes(bad).hex(), replies=[r['hex'] for r in replies])
    query('recovery_check', 0x4a, timeout=.6)
    send(frame(0x4e, b'\x00\x01'), 'interruption_info_ch1')
    receive(.15)
    send(frame(0x4e, b'\x00\x02'), 'interruption_info_ch2')
    receive(4.3)
    query('post_interruption_check', 0x4a, timeout=.6)
    print('READ_MATRIX_COMPLETE', flush=True)
    for mask in [1, 2, 3]:
        payload = mask.to_bytes(2, 'big')
        for name, key in [('stop', 0x10), ('up', 0x20), ('down', 0x40), ('tilt', 0x24), ('intermediate', 0x44),
                          ('experimental_up_21', 0x21), ('experimental_down_41', 0x41)]:
            query(f'send_mask_{mask}_{name}', 0x4c, payload + bytes([key]))
            if key != 0x10:
                query(f'cleanup_mask_{mask}_{name}', 0x4c, payload + b'\x10')
    query('final_version', 0x41, timeout=.6)
    query('final_channels', 0x4a, timeout=.6)
    print('MATRIX_COMPLETE', flush=True)
finally:
    try:
        send(frame(0x4c, b'\x00\x03\x10'), 'final_stop_paired_channels')
        receive(4.25)
    finally:
        s.close()
        stream.close()
