#!/usr/bin/env python3
"""Verify the external SPI/GPIO interface against actual project firmware.

Requires espemu.yaml running in esp-emulator and espemu_radio.py attached.
This is an emulator capability probe, separate from the HA integration suite.
"""

import argparse
import json
import time
import urllib.request
from pathlib import Path

from protocol import decode, status
from websockets.sync.client import connect


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--firmware-log", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    proof = []

    def record(check, data):
        proof.append({"check": check, "data": data})
        (args.output / "espemu-probe.json").write_text(json.dumps(proof, indent=2))
        print(f"PASS {check}", flush=True)

    def model(op="status", **data):
        request = urllib.request.Request(
            "http://127.0.0.1:28060",
            data=json.dumps({"op": op, **data}).encode(),
            headers={"Content-Type": "application/json"},
        )
        with urllib.request.urlopen(request, timeout=10) as response:
            return json.load(response)

    initial = model()
    assert initial["accepted"] and initial["transfers"] > 100, initial
    assert initial["registers"][11] == 8, initial
    assert initial["registers"][13:16] == [0x21, 0x71, 0x7A], initial
    assert initial["cs_edges"] > 100, initial
    record("production_cc1101_spi_setup_and_software_cs", initial)

    with connect("ws://127.0.0.1:28080/elero/ws", open_timeout=30, ping_interval=None) as ws:
        deadline = time.monotonic() + 120

        def receive(event, predicate=lambda _: True):
            while time.monotonic() < deadline:
                message = json.loads(ws.recv(timeout=max(1, deadline - time.monotonic())))
                if message["event"] == "error":
                    raise AssertionError(message)
                if message["event"] == event and predicate(message["data"]):
                    return message["data"]
            raise AssertionError(f"No {event} event")

        config = receive("config")
        assert config["radio"]["chipset"] == "cc1101", config
        record("production_web_server_over_emulated_wifi", config)
        snapshot = {
            "snapshot_version": 2,
            "devices": [
                {
                    "device_type": "cover",
                    "dst_address": "0xa831e5",
                    "src_address": "0xf0d008",
                    "channel": 1,
                    "name": "Emulator Probe Blind",
                    "enabled": True,
                }
            ],
            "groups": [],
            "hub": {},
        }
        ws.send(json.dumps({"type": "import_config", "snapshot": snapshot}))
        imported = receive("import_result")
        assert not imported["errors"] and imported["added"] + imported["updated"] == 1, imported
        record("production_nvs_import", imported)

        before = len(initial["tx"])
        ws.send(json.dumps({"type": "cmd", "address": "0xa831e5", "action": "up"}))
        found = None
        while time.monotonic() < deadline:
            current = model()
            for frame in current["tx"][before:]:
                parsed = decode(frame)
                if parsed["command"] == 0x20:
                    found = parsed
                    break
            if found:
                break
            time.sleep(0.1)
        assert found and found["src"] == 0xF0D008, current
        record("production_tx_through_spi_fifo", found)

        # Let the command repeat and CHECK sequence finish before delivering a
        # receiver frame. The radio is half duplex and SIDLE flushes its RX FIFO.
        while time.monotonic() < deadline:
            current = model()
            if current["sim_ns"] - initial["sim_ns"] > 2_000_000_000:
                break
            time.sleep(0.1)
        frame = status(0xA831E5, 1, 77)
        before_rx = model()["rx_reads"]
        model("rx", frame=frame)
        packet = receive("rf", lambda value: int(value["src"], 16) == 0xA831E5)
        assert int(packet["state"], 16) == 1, packet
        assert model()["rx_reads"] - before_rx == len(frame)
        record("production_rx_through_spi_fifo_and_decoder", packet)
        before_gpio = args.firmware_log.read_text().count("ESP_EMU_GPIO_IRQ")
        model("gpio")
        while time.monotonic() < deadline:
            if args.firmware_log.read_text().count("ESP_EMU_GPIO_IRQ") > before_gpio:
                break
            time.sleep(0.1)
        assert args.firmware_log.read_text().count("ESP_EMU_GPIO_IRQ") > before_gpio
        record("firmware_gpio_interrupt_handler", {"pin": 8, "console_marker": "ESP_EMU_GPIO_IRQ"})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
