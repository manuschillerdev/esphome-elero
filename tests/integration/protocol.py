"""Independent RF fixture encoder and TX decoder for integration assertions.

The wire algorithm is documented in .pi/skills/elero-protocol/SKILL.md.
No production functions are imported. Bytes are saved in the evidence log.
"""

ENCODE = (8, 2, 13, 1, 15, 14, 7, 5, 9, 12, 0, 10, 3, 4, 11, 6)
DECODE = tuple(ENCODE.index(i) for i in range(16))


def shift_nibbles(value, delta):
    high_delta = (delta & 0xF0) if delta >= 0 else -((-delta) & 0xF0)
    return (((value & 0xF0) + high_delta) & 0xF0) | ((value + delta) & 15)


def encode(payload):
    data = list(payload)
    parity = 0
    for i in range(0, 8, 2):
        parity = (parity | ((data[i].bit_count() ^ data[i + 1].bit_count()) & 1)) << 1
    data[7] = (parity << 3) & 255
    key = data[:2]
    for i in range(8):
        data[i] = shift_nibbles(data[i], (0xFE - i * 0x22) & 255)
        if i >= 2:
            data[i] ^= key[i % 2]
        data[i] = ENCODE[data[i] >> 4] * 16 + ENCODE[data[i] & 15]
    return data


def decode(frame):
    if len(frame) < 28 or frame[0] != len(frame) - 1:
        raise AssertionError(f"Invalid TX frame length: {frame}")
    if frame[2] not in (0x44, 0x6A):
        raise AssertionError(f"Unexpected TX packet type: {frame}")
    address_envelope = frame[2] in (0x6A, 0x69, 0xCA, 0xC9)
    count = frame[16]
    offset = 19 + count * (3 if address_envelope else 1)
    if not count or offset + 8 != len(frame):
        raise AssertionError(f"Invalid TX destination/payload length: {frame}")
    data = [DECODE[x >> 4] * 16 + DECODE[x & 15] for x in frame[offset : offset + 8]]
    for i in range(2):
        data[i] = shift_nibbles(data[i], -((0xFE - i * 0x22) & 255))
    key = data[:2]
    for i in range(8):
        data[i] ^= key[i % 2]
        if i >= 2:
            data[i] = shift_nibbles(data[i], -((0xFE - i * 0x22) & 255))
    destinations = []
    width = 3 if address_envelope else 1
    for i in range(count):
        destinations.append(int.from_bytes(bytes(frame[17 + i * width : 17 + (i + 1) * width]), "big"))
    return {
        "type": frame[2],
        "src": int.from_bytes(bytes(frame[7:10]), "big"),
        "destinations": destinations,
        "channel": frame[6],
        "command": data[2],
        "state": data[6],
        "raw": frame,
    }


def status(address, state, counter=1, *, crc_ok=True, channel=1):
    source = list(address.to_bytes(3, "big"))
    controller = list((0xF0D008).to_bytes(3, "big"))
    key = (-counter * 0x708F) & 65535
    payload = [key >> 8, key & 255, 0, 0, 0, 0, state, 0]
    frame = [29, counter, 0xCA, 0, 10, 1, channel] + source * 3 + [1] + controller + [0, 3] + encode(payload)
    return frame + [180, 0xB0 if crc_ok else 0x30]
