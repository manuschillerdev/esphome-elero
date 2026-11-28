#!/usr/bin/env python3
"""Small CC1101 SPI/FIFO model for testing esp-emulator's external interface.

This verifies the emulator interface with the production CC1101 driver. It
does not model the RF channel, analogue behavior, or every CC1101 register.
Wire contract: espressif/esp-emulator proto/esp_cosim.proto, version 1.
"""

import json
import os
import socket
import struct
import threading
import time
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

PORT_OFFSET = int(os.environ.get("ELERO_PORT_OFFSET", "0"))
LOG = Path(os.environ.get("ELERO_EVIDENCE", "/workspace/evidence")) / "espemu-spi.jsonl"


def varint(value):
    result = bytearray()
    while value > 127:
        result.append((value & 127) | 128)
        value >>= 7
    result.append(value)
    return bytes(result)


def scalar(field, value):
    return varint(field << 3) + varint(value)


def blob(field, value):
    return varint((field << 3) | 2) + varint(len(value)) + value


def unpack(data):
    index = 0
    result = {}

    def integer():
        nonlocal index
        value = shift = 0
        while True:
            byte = data[index]
            index += 1
            value |= (byte & 127) << shift
            if byte < 128:
                return value
            shift += 7

    while index < len(data):
        tag = integer()
        field, wire = tag >> 3, tag & 7
        if wire == 0:
            value = integer()
        else:
            size = integer() if wire == 2 else {1: 8, 5: 4}[wire]
            value = data[index : index + size]
            index += size
        result[field] = value
    return result


def read_exact(connection, size):
    output = bytearray()
    while len(output) < size:
        part = connection.recv(size - len(output))
        if hasattr(socket, "TCP_QUICKACK"):
            connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_QUICKACK, 1)
        if not part:
            raise EOFError(f"Emulator closed co-simulation connection: expected {size}, received {output.hex()}")
        output.extend(part)
    return bytes(output)


class Radio:
    def __init__(self):
        self.registers = bytearray(64)
        self.rx = deque()
        self.tx = []
        self.sent = []
        self.pending_tx = deque()
        self.address = None
        self.reading = False
        self.burst = False
        self.state = 1
        self.tx_reads = 0
        self.last_tx_ns = 0
        self.now = 0
        self.accepted = False
        self.transfers = 0
        self.cs_edges = 0
        self.rx_reads = 0
        self.fail_tx = False
        self.failed_strobes = 0
        self.clock = {}
        self.lock = threading.RLock()
        self.send_lock = threading.Lock()
        self.connection = None

    def log(self, kind, **data):
        with LOG.open("a") as log:
            log.write(json.dumps(dict(kind=kind, sim_ns=self.now, **data)) + "\n")

    def send(self, message):
        assert self.connection is not None
        with self.send_lock:
            self.connection.sendall(struct.pack("<I", len(message)) + message)

    def drive(self, pin, level, delay_ms):
        self.send(scalar(1, self.now + delay_ms * 1_000_000) + blob(6, scalar(1, pin) + scalar(2, int(level))))

    def transfer(self, data):
        output = bytearray()
        for byte in data:
            if self.address is None:
                output.append(0x10 if self.state == 13 else 0)
                if 0x30 <= byte <= 0x3D:
                    if byte == 0x30:
                        self.registers[:] = bytes(64)
                        self.state = 1
                    elif byte == 0x34:
                        self.state = 13
                    elif byte == 0x35:
                        self.last_tx_ns = self.now
                        if self.fail_tx:
                            self.failed_strobes += 1
                            self.log("failed_stx", frame=self.tx)
                            continue
                        self.state = 19
                        self.tx_reads = 0
                        self.sent.append(list(self.tx))
                        self.pending_tx.append(list(self.tx))
                        self.log("tx_fifo", frame=self.tx)
                    elif byte == 0x36:
                        self.state = 1
                    elif byte == 0x3A:
                        self.rx.clear()
                    elif byte == 0x3B:
                        self.tx.clear()
                    continue
                self.address = byte & 63
                self.reading = bool(byte & 128)
                self.burst = bool(byte & 64)
            elif self.reading:
                address = self.address
                if address == 0x30:
                    value = 0
                elif address == 0x31:
                    value = 0x14
                elif address == 0x35:
                    value = self.state
                    if self.state == 19:
                        self.tx_reads += 1
                        if self.tx_reads > 1:
                            self.state = 13
                            self.tx.clear()
                elif address == 0x38:  # PKTSTATUS.CCA on the idle test channel
                    value = 0x10 if self.state == 13 and not self.rx else 0
                elif address == 0x3A:
                    value = len(self.tx)
                elif address == 0x3B:
                    value = len(self.rx)
                elif address == 0x3F:
                    value = self.rx.popleft() if self.rx else 0
                    self.rx_reads += 1
                else:
                    value = self.registers[address]
                output.append(value)
                if self.burst and address < 0x30:
                    self.address += 1
            else:
                output.append(0)
                if self.address == 0x3F:
                    self.tx.append(byte)
                else:
                    self.registers[self.address] = byte
                    if self.burst and self.address < 0x30:
                        self.address += 1
        return output

    def run(self):
        for _ in range(200):
            try:
                self.connection = socket.create_connection(("127.0.0.1", 28590 + PORT_OFFSET), timeout=1)
                self.connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                self.connection.settimeout(None)
                break
            except OSError:
                time.sleep(0.1)
        else:
            raise RuntimeError("No emulator co-simulation server")
        spi = b"".join(scalar(field, pin) for field, pin in ((1, 10), (2, 12), (3, 11), (4, 13)))
        gpio = scalar(1, (1 << 9) | (1 << 8)) + scalar(2, 1 << 10)
        registration = scalar(1, 1) + blob(2, b"elero-cc1101-probe")
        registration += blob(3, blob(3, spi)) + blob(3, blob(1, gpio))
        self.send(blob(4, registration))
        while True:
            length = struct.unpack("<I", read_exact(self.connection, 4))[0]
            if not self.accepted:
                print(f"co-simulation frame: {length} bytes", flush=True)
            if length > 262144:
                raise ValueError(f"Invalid co-simulation frame length: {length}")
            frame = read_exact(self.connection, length)
            topic_length = struct.unpack("<H", frame[:2])[0]
            message = unpack(frame[2 + topic_length :])
            with self.lock:
                self.now = message.get(1, self.now)
                if 5 in message:
                    ack = unpack(message[5])
                    self.accepted = bool(ack.get(5, 0))
                    self.log("registration", accepted=self.accepted, ack=repr(ack))
                    if not self.accepted:
                        raise RuntimeError(f"Registration refused: {ack}")
                    self.drive(9, False, 10)
                    self.drive(8, False, 10)
                if 2 in message:
                    self.clock = unpack(message[2])
                if 3 in message:
                    gpio = unpack(message[3])
                    if gpio.get(1) == 10:
                        self.address = None
                        self.cs_edges += 1
                if 9 in message:
                    self.now = 0
                    self.last_tx_ns = 0
                    self.address = None
                    self.log("chip_reset")
                if 7 not in message:
                    continue
                request = unpack(message[7])
                spi = unpack(request[3])
                tx = spi.get(3, b"")
                received = self.transfer(tx)
                rx_size = spi.get(4, 0)
                self.transfers += 1
                if self.transfers < 200:
                    self.log("spi_transfer", tx=list(tx), rx=list(received), rx_size=rx_size, pin_levels=spi.get(5))
                if rx_size:
                    reply = scalar(1, request[1]) + scalar(2, 1) + blob(3, bytes(received[:rx_size])) + scalar(4, 2)
                    self.send(blob(8, reply))


RADIO = Radio()


class Control(BaseHTTPRequestHandler):
    def do_POST(self):
        request = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        with RADIO.lock:
            if request["op"] == "rx":
                RADIO.rx.extend(request["frame"])
                RADIO.drive(9, True, 10)
                RADIO.drive(9, False, 20)
                RADIO.log("injected_rx", frame=request["frame"])
            elif request["op"] == "gpio":
                RADIO.drive(8, True, 10)
                RADIO.drive(8, False, 40)
            elif request["op"] == "fail_tx":
                RADIO.fail_tx = bool(request["value"])
            result = {
                "ok": RADIO.accepted,
                "overflows": 0,
                "accepted": RADIO.accepted,
                "transfers": RADIO.transfers,
                "cs_edges": RADIO.cs_edges,
                "rx_reads": RADIO.rx_reads,
                "last_tx_ns": RADIO.last_tx_ns,
                "radio_state": RADIO.state,
                "rx_pending": len(RADIO.rx),
                "failed_strobes": RADIO.failed_strobes,
                "clock": RADIO.clock,
                "sim_ns": RADIO.now,
            }
            if request["op"] == "status":
                result["registers"] = list(RADIO.registers)
                result["tx"] = RADIO.sent
            if request["op"] == "tx":
                result["frames"] = [RADIO.pending_tx.popleft() for _ in range(min(4, len(RADIO.pending_tx)))]
        encoded = json.dumps(result).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def log_message(self, format, *args):
        pass


if __name__ == "__main__":
    threading.Thread(target=RADIO.run, daemon=True).start()
    ThreadingHTTPServer(("127.0.0.1", 28060 + PORT_OFFSET), Control).serve_forever()
