#!/usr/bin/env python3
"""Run inside the test VM. Exercise real HA, firmware, NVS and RF packets."""

import argparse
import asyncio
import contextlib
import json
import os
import shutil
import signal
import socket
import time
import traceback
import xml.etree.ElementTree as ET
from collections import deque
from pathlib import Path

import aiohttp
from protocol import decode, status

WORK = Path("/workspace")
EVIDENCE = WORK / "evidence"
HA_IMAGE = "ghcr.io/home-assistant/home-assistant:2026.11.0"
COVER = 0xA831E5
LIGHT = 0xA831E6
STEP = 0xA831E7
PLAIN = 0xA831E8
OTHER_LIGHT = 0xA831E9
CONTROLLER = "0xf0d008"
ACTIONS = ("Short press up", "Short press down", "Full up", "Full down")


def device(address, name, kind="cover", **extra):
    return dict(
        dst_address=f"0x{address:06x}",
        src_address=CONTROLLER,
        channel=address - COVER + 1,
        device_type=kind,
        name=name,
        enabled=True,
        **extra,
    )


DEVICES = [
    device(COVER, "Test Blind", open_duration_ms=2000, close_duration_ms=2000, supports_tilt=True),
    device(LIGHT, "Test Light", "light", dim_duration_ms=2000),
    device(STEP, "Step Blind", movement_mode="step", supports_tilt=True, tilt_mode="step"),
    device(PLAIN, "Plain Blind", open_duration_ms=2000, close_duration_ms=2000),
    device(OTHER_LIGHT, "Group Light", "light", dim_duration_ms=2000),
]
GROUPS = [
    {"id": "test_group", "name": "Test Group", "device_ids": [f"0x{COVER:06x}", f"0x{PLAIN:06x}"]},
    {"id": "light_group", "name": "Light Group", "device_ids": [f"0x{LIGHT:06x}", f"0x{OTHER_LIGHT:06x}"]},
]
SNAPSHOT = {"snapshot_version": 2, "hub": {}, "devices": DEVICES, "groups": GROUPS}


async def command(*args, check=True):
    process = await asyncio.create_subprocess_exec(
        *args, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT
    )
    try:
        output, _ = await asyncio.wait_for(process.communicate(), 120)
    except BaseException:
        process.kill()
        await process.wait()
        raise
    text = output.decode(errors="replace")
    with (EVIDENCE / "commands.log").open("a") as log:
        log.write(f"{args!r}\n{text}\n")
    if check and process.returncode:
        raise AssertionError(text)
    return text


async def wait_for(probe, description, timeout=120):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        try:
            last = await probe()
            if last:
                return last
        except (aiohttp.ClientError, TimeoutError, ConnectionError) as error:
            last = str(error)
        await asyncio.sleep(0.3)
    raise AssertionError(f"Timeout: {description}; last result: {last!r}")


class Suite:
    def __init__(self, mode, session):
        self.mode = mode
        self.session = session
        self.token = None
        self.counter = 0
        self.events = []
        self.pending_tx = deque()
        self.event_task = None
        self.entities = {}
        self.initial_states = {}
        self.case = "startup"
        self.processes = []
        self.log_files = []
        self.path = EVIDENCE / mode
        self.path.mkdir(parents=True, exist_ok=True)

    def record(self, kind, data):
        with (self.path / "proof.jsonl").open("a") as log:
            log.write(json.dumps({"time": time.time(), "case": self.case, "kind": kind, "data": data}) + "\n")

    async def http(self, method, path, data=None, *, auth=True, form=None, missing_ok=False):
        headers = {"Authorization": f"Bearer {self.token}"} if auth and self.token else {}
        async with self.session.request(
            method, "http://127.0.0.1:8123" + path, json=data, data=form, headers=headers
        ) as response:
            text = await response.text()
            if response.status == 404 and missing_ok:
                return None
            if response.status >= 400:
                raise AssertionError(f"HA {method} {path}: {response.status} {text}")
            return json.loads(text) if text else None

    async def radio(self, op, **data):
        request = dict(op=op, **data)
        async with self.session.post("http://127.0.0.1:28060", json=request) as response:
            result = await response.json()
        if not result.get("ok") or result.get("overflows"):
            raise AssertionError(f"Radio control failed: {request} {result}")
        if op != "ping":
            self.record("radio_control", {"request": request, "response": result})
        return result

    async def radio_ready(self):
        try:
            return await self.radio("ping")
        except AssertionError:
            # The model HTTP server can start before the emulator accepts its
            # device registration. Only this readiness probe retries that case.
            return False

    async def firmware(self, message, expected):
        async with self.session.ws_connect("http://127.0.0.1:18080/elero/ws") as ws:
            await ws.send_json(message)
            async with asyncio.timeout(45):
                async for response in ws:
                    if response.type != aiohttp.WSMsgType.TEXT:
                        continue
                    value = json.loads(response.data)
                    if value["event"] == "error":
                        if expected == "error":
                            return value["data"]
                        raise AssertionError(value)
                    if value["event"] == expected:
                        self.record("firmware_response", value)
                        return value["data"]
        raise AssertionError(f"Missing firmware response: {expected}")

    async def rx(self, address, value, **kwargs):
        self.counter = self.counter % 254 + 1
        frame = status(address, value, self.counter, channel=address - COVER + 1, **kwargs)
        await self.inject(frame)

    async def inject(self, frame):
        async def receiver_ready():
            observed = await self.radio("ping")
            return (
                observed
                if (observed["radio_state"] == 13 and observed["sim_ns"] - observed["last_tx_ns"] >= 150_000_000)
                else None
            )

        # Reports are scripted after the TX burst. Injecting between repeats
        # races the production driver's intentional RX flush before each TX.
        ready = await wait_for(receiver_ready, "radio RX after TX burst", timeout=30)
        before = ready["rx_reads"]
        await self.radio("rx", frame=frame)

        async def drained():
            observed = await self.radio("ping")
            return observed["rx_pending"] == 0 and observed["rx_reads"] >= before + len(frame)

        # A CC1101 FIFO is 64 bytes. Wait for the production driver to drain
        # this frame before delivering another; the model is not an RX queue.
        await wait_for(drained, "CC1101 RX FIFO drain", timeout=30)

    async def state(self, entity, predicate=None):
        value = await self.http("GET", f"/api/states/{entity}", missing_ok=True)
        if value is None:
            return None
        return value if predicate is None or predicate(value) else None

    async def expect_state(self, entity, value, *, timeout=10):
        result = await wait_for(
            lambda: self.state(entity, lambda state: state["state"] == value), f"{entity} = {value}", timeout=timeout
        )
        self.record("ha_asserted_state", result)
        return result

    async def service(self, entity, service, **data):
        request = dict(entity_id=entity, **data)
        self.record("ha_service", {"service": service, "request": request})
        return await self.http("POST", f"/api/services/{entity.split('.')[0]}/{service}", request)

    async def rx_state(self, address, feedback, entity, expected):
        """Require a new HA event after RX, as well as the resulting REST state.

        Use this for transitions: an unchanged state need not produce an event.
        """
        before = len(self.events)
        await self.rx(address, feedback)

        async def observed():
            for item in self.events[before:]:
                data = item.get("event", {}).get("data", {})
                state = data.get("new_state") or {}
                if data.get("entity_id") == entity and state.get("state") == expected:
                    return state
            return None

        self.record("ha_asserted_rx_event", await wait_for(observed, f"new RX state event: {entity} = {expected}"))
        return await self.expect_state(entity, expected)

    async def clear_tx(self):
        self.pending_tx.clear()
        for _ in range(40):
            if not (await self.radio("tx"))["frames"]:
                return
        raise AssertionError("TX stream did not drain")

    async def expect_tx(self, expected, *, address=None, destinations=None, packet_type=0x44, channel=None, timeout=20):
        # Channel envelopes address receivers by channel selector, not RF address.
        if address is not None:
            channel = address - COVER + 1 if channel is None else channel
            destinations = [address] if packet_type == 0x6A else [channel]
        if destinations is None or channel is None:
            raise AssertionError("TX assertion requires an exact destination list and channel")
        seen = []

        async def probe():
            if not self.pending_tx:
                self.pending_tx.extend((await self.radio("tx"))["frames"])
            while self.pending_tx:
                parsed = decode(self.pending_tx.popleft())
                seen.append(parsed)
                if parsed["command"] != expected:
                    self.record("other_tx", parsed)
                    continue
                assert parsed["src"] == int(CONTROLLER, 16), parsed
                assert parsed["destinations"] == destinations, parsed
                assert parsed["type"] == packet_type, parsed
                assert parsed["channel"] == channel, parsed
                self.record("asserted_tx", parsed)
                return parsed
            return None

        try:
            return await wait_for(probe, f"TX command 0x{expected:02x}", timeout=timeout)
        except AssertionError as error:
            raise AssertionError(f"{error}; decoded TX: {seen}") from error

    async def connect_events(self):
        ws = await self.session.ws_connect("http://127.0.0.1:8123/api/websocket")
        assert (await ws.receive_json())["type"] == "auth_required"
        await ws.send_json({"type": "auth", "access_token": self.token})
        assert (await ws.receive_json())["type"] == "auth_ok"
        await ws.send_json({"id": 1, "type": "subscribe_events", "event_type": "state_changed"})
        assert (await ws.receive_json())["success"]

        async def collect():
            try:
                async for message in ws:
                    if message.type == aiohttp.WSMsgType.TEXT:
                        event = json.loads(message.data)
                        self.events.append(event)
                        self.record("ha_event", event)
            finally:
                await ws.close()

        self.event_task = asyncio.create_task(collect())

    async def wait_mqtt_commands(self, cursor, configs, *, groups=False):
        topics = set()
        for config in configs:
            if not config.get("enabled", True):
                continue
            kind = config["device_type"]
            if kind not in ("cover", "light"):
                continue
            address = int(config["dst_address"], 16)
            suffixes = ("set", "set_position", "tilt") if kind == "cover" else ("set",)
            topics.update(f"elero/{kind}/{address:06x}/{suffix}" for suffix in suffixes)
        if groups:
            topics.update(f"elero/group/{group['id']}/set" for group in GROUPS)

        async def subscribed():
            lines = (await command("docker", "logs", "broker")).splitlines()[cursor:]
            acknowledged = set()
            for index, line in enumerate(lines):
                for topic in topics:
                    if f"{topic} (QoS 0)" in line and any(
                        "Sending SUBACK to elero-e2e-" in ack for ack in lines[index + 1 : index + 4]
                    ):
                        acknowledged.add(topic)
            return acknowledged == topics

        await wait_for(subscribed, "all firmware command subscriptions acknowledged", timeout=90)
        self.record("broker_command_subscriptions", sorted(topics))

    async def ha_ws(self, kind):
        async with self.session.ws_connect("http://127.0.0.1:8123/api/websocket") as ws:
            assert (await ws.receive_json())["type"] == "auth_required"
            await ws.send_json({"type": "auth", "access_token": self.token})
            assert (await ws.receive_json())["type"] == "auth_ok"
            await ws.send_json({"id": 1, "type": kind})
            result = await ws.receive_json()
            assert result.get("success"), result
            return result["result"]

    async def inventory(self, extra=(), groups=()):
        """Check the live Elero inventory separately from retained HA registry rows."""
        domain = "mqtt" if self.mode == "mqtt" else "esphome"
        entries = [entry for entry in await self.ha_ws("config_entries/get") if entry["domain"] == domain]
        assert len(entries) == 1 and entries[0]["state"] == "loaded", entries
        entry_id = entries[0]["entry_id"]
        entities = [e for e in await self.ha_ws("config/entity_registry/list") if e["config_entry_id"] == entry_id]
        devices = {d["id"]: d for d in await self.ha_ws("config/device_registry/list")}
        states = {v["entity_id"]: v for v in await self.http("GET", "/api/states")}
        self.record("ha_registry_snapshot", {"entries": entries, "entities": entities, "devices": devices})
        live = [e for e in entities if e["entity_id"] in states and states[e["entity_id"]]["state"] != "unavailable"]
        expected = []
        for config in [*DEVICES, *extra]:
            name, kind = config["name"], config["device_type"]
            owner = name if self.mode == "mqtt" else "Elero E2E"
            if kind == "remote":
                if self.mode == "mqtt":
                    expected.append(("sensor", name, owner))
                else:
                    expected.extend(("sensor", f"{name} {label}", owner) for label in ("RSSI", "Last command"))
                continue
            expected.append((kind, name, owner))
            if kind == "cover":
                expected.extend(
                    ("button", action if self.mode == "mqtt" else f"{name} {action}", owner) for action in ACTIONS
                )
            for domain, label in (("sensor", "RSSI"), ("sensor", "State"), ("binary_sensor", "Problem")):
                expected.append((domain, label if self.mode == "mqtt" else f"{name} {label}", owner))
        expected.extend([("cover", "Test Group", "Elero E2E"), ("light", "Light Group", "Elero E2E")])
        expected.extend((kind, name, "Elero E2E") for kind, name in groups)
        if self.mode == "mqtt":
            expected.append(("sensor", "Active devices", "Elero E2E"))

        def key(e):
            d = devices[e["device_id"]]
            return e["entity_id"].split(".")[0], e["original_name"], d["name"]

        observed = [key(e) for e in live]
        assert sorted(observed) == sorted(expected), {"expected": expected, "observed": observed}
        assert len({e["unique_id"] for e in entities}) == len(entities), entities
        result = {}
        for e in live:
            assert e["platform"] == domain and e["disabled_by"] is None and e["unique_id"], e
            d = devices[e["device_id"]]
            assert entry_id in d["config_entries"], d
            result[key(e)] = {field: e[field] for field in ("id", "entity_id", "unique_id", "device_id")}
        hub_ids = {
            e["device_id"]
            for e in live
            if e["entity_id"].split(".")[0] == "cover" and e["original_name"] == "Test Group"
        }
        assert len(hub_ids) == 1, hub_ids
        hub_id = hub_ids.pop()
        used_devices = {e["device_id"] for e in live}
        assert len(used_devices) == (1 + len(DEVICES) + len(extra) if self.mode == "mqtt" else 1), used_devices
        if self.mode == "mqtt":
            for device_id in used_devices - {hub_id}:
                assert devices[device_id]["via_device_id"] == hub_id, devices[device_id]
        self.record("ha_asserted_inventory", {"count": len(result), "entities": list(result.values())})
        return result

    async def wait_inventory(self, extra=(), groups=()):
        # Discovery and native enumeration update several HA entities in sequence.
        last = None

        async def complete():
            nonlocal last
            try:
                return await self.inventory(extra, groups)
            except AssertionError as error:
                last = str(error)
                return None

        try:
            return await wait_for(complete, "complete HA entity/device inventory", timeout=10)
        except AssertionError as error:
            raise AssertionError(f"{error}; last inventory: {last}") from error

    async def seed_receivers(self, *, transition_lights=False):
        for config in DEVICES:
            address = int(config["dst_address"], 16)
            if config["device_type"] == "light":
                if transition_lights:
                    await self.rx(address, 0x11)
                await self.rx(address, 0x10)
            else:
                await self.rx(address, 2)
        for key in ("cover", "step", "plain", "group"):
            await self.expect_state(self.entities[key], "closed")
        for key in ("light", "other_light"):
            await self.expect_state(self.entities[key], "off")

    async def test_entity_inventory(self):
        self.identity_baseline = await self.wait_inventory()

    async def test_action_buttons(self):
        inventory = await self.wait_inventory()
        for address, name in ((COVER, "Test Blind"), (STEP, "Step Blind"), (PLAIN, "Plain Blind")):
            for action, cmd in zip(ACTIONS, (0x20, 0x40, 0x21, 0x41), strict=True):
                owner = name if self.mode == "mqtt" else "Elero E2E"
                key = ("button", action if self.mode == "mqtt" else f"{name} {action}", owner)
                await self.clear_tx()
                await self.service(inventory[key]["entity_id"], "press")
                await self.expect_tx(cmd, address=address, packet_type=0x44 if action.startswith("Short") else 0x6A)
                if action.startswith("Short"):
                    await self.expect_tx(0, address=address)
                await self.rx(address, 2)
        # A restart must not add another set of action buttons.
        assert await self.wait_inventory() == inventory

    async def retained_discovery(self, address, expected, kind="cover"):
        text = await command(
            "docker",
            "exec",
            "broker",
            "mosquitto_sub",
            "-t",
            "homeassistant/#",
            "--retained-only",
            "-W",
            "1",
            "-F",
            "%t %p",
            check=False,
        )
        rows = [line for line in text.splitlines() if f"_{kind}_{address:06x}" in line]
        assert len(rows) == expected, rows
        self.record("ha_asserted_retained_discovery", {"address": address, "expected": expected, "rows": rows})

    async def test_group_position(self):
        await self.seed_receivers()
        entity = self.entities["group"]
        assert (await self.state(entity))["attributes"]["supported_features"] & 4
        await self.clear_tx()
        await self.service(entity, "set_cover_position", position=50)
        reached = set()

        async def transmitted(reached=reached):
            for raw in (await self.radio("tx"))["frames"]:
                frame = decode(raw)
                cmd = frame["command"]
                if cmd not in (0x20, 0x10):
                    continue
                assert frame["src"] == int(CONTROLLER, 16), frame
                assert frame["type"] == (0x44 if cmd == 0x20 else 0x6A), frame
                assert len(frame["destinations"]) == 1, frame
                destination = frame["destinations"][0]
                address = destination if cmd == 0x10 else COVER + destination - 1
                assert address in (COVER, PLAIN) and frame["channel"] == address - COVER + 1, frame
                reached.add((cmd, address))
                self.record("asserted_group_position_tx", frame)
            return reached == {(cmd, address) for cmd in (0x20, 0x10) for address in (COVER, PLAIN)}

        await wait_for(transmitted, "UP and timed STOP for both group members", timeout=60)
        for target in (entity, self.entities["cover"], self.entities["plain"]):
            await wait_for(
                lambda target=target: self.state(
                    target, lambda state: abs(state["attributes"].get("current_position", -100) - 50) <= 3
                ),
                f"group member position: {target}",
            )

    async def test_light_group(self):
        inventory = await self.wait_inventory()
        entity = inventory[("light", "Light Group", "Elero E2E")]["entity_id"]
        feedback_errors = []
        for service, cmd, feedback, final in (
            ("turn_on", 0x20, 0x11, "on"),
            ("turn_off", 0x40, 0x10, "off"),
            ("toggle", 0x20, 0x11, "on"),
        ):
            await self.clear_tx()
            await self.service(entity, service)
            await self.expect_tx(cmd, destinations=[2, 5], channel=0)
            await self.rx(LIGHT, feedback)
            await self.rx(OTHER_LIGHT, feedback)
            try:
                await self.expect_state(entity, final, timeout=5)
            except AssertionError as error:
                feedback_errors.append(str(error))
                self.record("ha_light_group_feedback_failure", {"service": service, "error": str(error)})
        await self.seed_receivers()
        self.clean_after_failure = True
        assert not feedback_errors, feedback_errors

    async def test_integration_readd(self):
        before = await self.wait_inventory()
        domain = "mqtt" if self.mode == "mqtt" else "esphome"
        entry = next(e for e in await self.ha_ws("config_entries/get") if e["domain"] == domain)
        result = await self.http("DELETE", f"/api/config/config_entries/entry/{entry['entry_id']}")
        assert not result.get("require_restart"), result

        async def removed():
            entries = await self.ha_ws("config_entries/get")
            entities = await self.ha_ws("config/entity_registry/list")
            devices = await self.ha_ws("config/device_registry/list")
            states = await self.http("GET", "/api/states")
            old_ids = {v["entity_id"] for v in before.values()}
            old_registry_ids = {v["id"] for v in before.values()}
            old_device_ids = {v["device_id"] for v in before.values()}
            return (
                not any(e["domain"] == domain for e in entries)
                and not any(e["config_entry_id"] == entry["entry_id"] or e["id"] in old_registry_ids for e in entities)
                and not any(entry["entry_id"] in d["config_entries"] or d["id"] in old_device_ids for d in devices)
                and not any(state["entity_id"] in old_ids for state in states)
            )

        await wait_for(removed, "HA integration, entities and device associations removed")
        self.record(
            "ha_asserted_integration_removed", {"entry_id": entry["entry_id"], "entities": list(before.values())}
        )
        await self.add_integration()
        after = await self.wait_inventory()
        for key, kind, name in (
            ("cover", "cover", "Test Blind"),
            ("light", "light", "Test Light"),
            ("other_light", "light", "Group Light"),
            ("step", "cover", "Step Blind"),
            ("plain", "cover", "Plain Blind"),
            ("group", "cover", "Test Group"),
        ):
            owner = name if self.mode == "mqtt" and key != "group" else "Elero E2E"
            self.entities[key] = after[(kind, name, owner)]["entity_id"]
        await self.seed_receivers(transition_lights=True)
        new = next(e for e in await self.ha_ws("config_entries/get") if e["domain"] == domain)
        assert new["entry_id"] != entry["entry_id"], new
        assert {key: value["unique_id"] for key, value in before.items()} == {
            key: value["unique_id"] for key, value in after.items()
        }, {"before": before, "after": after}
        self.identity_baseline = after
        self.record("ha_asserted_clean_integration_readd", {"old": entry["entry_id"], "new": new["entry_id"]})

    async def test_entity_lifecycle(self):
        baseline = await self.wait_inventory()
        configs = [
            device(COVER + 5, "Lifecycle Blind"),
            device(COVER + 6, "Lifecycle Light", "light", dim_duration_ms=2000),
            device(COVER + 7, "Lifecycle Remote", "remote"),
        ]

        async def apply(values, *, added=False):
            cursor = len((await command("docker", "logs", "broker")).splitlines()) if self.mode == "mqtt" else 0
            result = await self.firmware(
                {"type": "import_config", "snapshot": {"snapshot_version": 2, "devices": values}}, "import_result"
            )
            assert result["added" if added else "updated"] == len(values) and not result["errors"], result
            if self.mode == "mqtt":
                await self.wait_mqtt_commands(cursor, values)

        async def settle(values):
            if self.mode == "native":
                await self.restart_firmware()
                await self.seed_receivers()
            for config in values:
                await self.rx(int(config["dst_address"], 16), 0x10 if config["device_type"] == "light" else 2)

        async def discovery(enabled):
            if self.mode == "mqtt":
                for config, count in zip(configs, (8, 4, 1), strict=True):
                    await self.retained_discovery(
                        int(config["dst_address"], 16), count if enabled else 0, config["device_type"]
                    )

        await apply(configs, added=True)
        if self.mode == "native":
            assert await self.wait_inventory() == baseline
        await settle(configs)
        added = await self.wait_inventory(configs)
        temporary = {k: v for k, v in added.items() if k not in baseline}
        assert len(temporary) == (13 if self.mode == "mqtt" else 14), temporary
        snapshot = await self.firmware({"type": "export_config"}, "config_snapshot")
        assert all(
            any(
                d["dst_address"] == c["dst_address"] and d["device_type"] == c["device_type"]
                for d in snapshot["devices"]
            )
            for c in configs
        )
        await discovery(True)
        self.record("ha_lifecycle_phase", {"phase": "added", "types": [c["device_type"] for c in configs]})

        await apply([{**c, "enabled": False} for c in configs])
        if self.mode == "native":
            assert await self.wait_inventory(configs) == added
        await settle([])
        assert await self.wait_inventory() == baseline
        await discovery(False)
        for entity in temporary.values():
            state = await self.state(entity["entity_id"])
            assert state is None or state["state"] == "unavailable", state
        self.record("ha_lifecycle_phase", {"phase": "disabled"})

        await apply(configs)
        if self.mode == "native":
            assert await self.wait_inventory() == baseline
        await settle(configs)
        assert await self.wait_inventory(configs) == added
        await discovery(True)
        for config, service, cmd in ((configs[0], "open_cover", 0x20), (configs[1], "turn_on", 0x20)):
            owner = config["name"] if self.mode == "mqtt" else "Elero E2E"
            entity = added[(config["device_type"], config["name"], owner)]["entity_id"]
            await self.clear_tx()
            await self.service(entity, service)
            await self.expect_tx(cmd, address=int(config["dst_address"], 16))
            await self.rx(int(config["dst_address"], 16), 1 if config["device_type"] == "cover" else 0x11)
            await self.expect_state(entity, "open" if config["device_type"] == "cover" else "on")
        self.record("ha_lifecycle_phase", {"phase": "enabled", "commands_verified": ["cover", "light"]})

        for config in configs:
            await self.firmware(
                {"type": "remove_device", "dst_address": config["dst_address"], "device_type": config["device_type"]},
                "device_removed",
            )
        if self.mode == "native":
            assert await self.wait_inventory(configs) == added
        await settle([])
        assert await self.wait_inventory() == baseline
        await discovery(False)
        for entity in temporary.values():
            state = await self.state(entity["entity_id"])
            assert state is None or state["state"] == "unavailable", state
        snapshot = await self.firmware({"type": "export_config"}, "config_snapshot")
        assert len(snapshot["devices"]) == len(DEVICES), snapshot
        self.record("ha_lifecycle_phase", {"phase": "deleted", "baseline_restored": True})

    async def test_rename(self):
        """Batch configuration changes, but check each entity's identity and name."""
        baseline = await self.wait_inventory()
        configs = [
            device(COVER + 5, "Rename Cover"),
            device(COVER + 6, "Rename Light", "light", dim_duration_ms=2000),
            device(COVER + 7, "Rename Remote", "remote"),
        ]
        renamed = [{**c, "name": c["name"].replace("Rename ", "Renamed ")} for c in configs]
        failures = []
        before = {}
        for values, added in ((configs, True), (renamed, False)):
            cursor = len((await command("docker", "logs", "broker")).splitlines()) if self.mode == "mqtt" else 0
            result = await self.firmware(
                {"type": "import_config", "snapshot": {"snapshot_version": 2, "devices": values}}, "import_result"
            )
            assert result["added" if added else "updated"] == len(values) and not result["errors"], result
            if self.mode == "native":
                assert await self.wait_inventory([] if added else configs) == (baseline if added else before)
                # Rename must work on natural reconnect, including HA metadata
                # caching. Reload only the initial add, not the rename itself.
                await self.restart_firmware(reload_api=added)
                await self.seed_receivers()
            else:
                await self.wait_mqtt_commands(cursor, values)
            for config in values:
                await self.rx(int(config["dst_address"], 16), 0x10 if config["device_type"] == "light" else 2)
            snapshot = await self.firmware({"type": "export_config"}, "config_snapshot")
            for config in values:
                restored = next(d for d in snapshot["devices"] if d["dst_address"] == config["dst_address"])
                assert restored["name"] == config["name"], restored
            if added:
                before = await self.wait_inventory(configs)
                continue
            # The API has reconnected and sent its initial states. Inspect all
            # kinds even when one name or identity is wrong; do not wait for a
            # known metadata mismatch or let it hide the other rename checks.
            entities = await self.ha_ws("config/entity_registry/list")
            by_id = {e["id"]: e for e in entities}
            states = {v["entity_id"]: v for v in await self.http("GET", "/api/states")}
            for old, new in zip(configs, renamed, strict=True):
                owner = old["name"] if self.mode == "mqtt" else "Elero E2E"
                originals = [
                    (k, v)
                    for k, v in before.items()
                    if k[2] == owner and (k[1] == old["name"] or k[1].startswith(old["name"] + " "))
                ]
                if self.mode == "mqtt":
                    originals = [(k, v) for k, v in before.items() if k[2] == owner]
                errors = []
                for key, identity in originals:
                    current = by_id.get(identity["id"])
                    expected_name = key[1].replace(old["name"], new["name"], 1)
                    if (
                        current is None
                        or current["entity_id"] not in states
                        or states[current["entity_id"]]["state"] == "unavailable"
                    ):
                        errors.append(f"{key[0]} identity disappeared: {identity['entity_id']}")
                        continue
                    if any(current[field] != identity[field] for field in ("entity_id", "unique_id", "device_id")):
                        errors.append(f"{key[0]} identity changed: {identity['entity_id']}")
                    if current["original_name"] != expected_name:
                        errors.append(f"{key[0]} name {current['original_name']!r}, expected {expected_name!r}")
                self.record(
                    "ha_rename_check",
                    {"kind": old["device_type"], "before": [v for _, v in originals], "errors": errors},
                )
                failures.extend(f"{old['device_type']}: {error}" for error in errors)
            if not failures:
                after = await self.wait_inventory(renamed)
                assert sorted(v["id"] for v in before.values()) == sorted(v["id"] for v in after.values())
        for config in configs:
            await self.firmware(
                {"type": "remove_device", "dst_address": config["dst_address"], "device_type": config["device_type"]},
                "device_removed",
            )
        if self.mode == "native":
            await self.restart_firmware()
            await self.seed_receivers()
        assert await self.wait_inventory() == baseline
        self.clean_after_failure = True
        assert not failures, failures

    async def test_cover_actions(self):
        entity = self.entities["cover"]
        await self.rx(COVER, 2)
        await self.expect_state(entity, "closed")
        await self.clear_tx()
        await self.service(entity, "toggle")
        await self.expect_tx(0x20, address=COVER)
        await self.rx(COVER, 1)
        await self.expect_state(entity, "open")
        await self.clear_tx()
        await self.service(entity, "toggle")
        await self.expect_tx(0x40, address=COVER)
        await self.rx(COVER, 2)
        await self.expect_state(entity, "closed")
        # Test the direct HA tilt services in addition to set_tilt_position.
        for key, address in (("cover", COVER), ("step", STEP)):
            for service, command, feedback in (
                ("open_cover_tilt", 0x24 if key == "cover" else 0x20, 4),
                ("close_cover_tilt", 0x24 if key == "cover" else 0x40, 2),
            ):
                await self.clear_tx()
                await self.service(self.entities[key], service)
                await self.expect_tx(command, address=address)
                if key == "step":
                    await self.expect_tx(0, address=address)
                await self.rx(address, feedback)
        # Only test STOP_TILT when HA actually advertises that feature.
        features = (await self.state(entity))["attributes"]["supported_features"]
        if features & 64:
            await self.clear_tx()
            await self.service(entity, "stop_cover_tilt")
            try:
                await self.expect_tx(0x10, address=COVER, packet_type=0x6A, timeout=5)
            except AssertionError:
                await self.seed_receivers()
                self.clean_after_failure = True
                raise

    async def test_light_actions(self):
        light = self.entities["light"]
        await self.rx(LIGHT, 0x10)
        await self.expect_state(light, "off")
        await self.clear_tx()
        await self.service(light, "toggle")
        await self.expect_tx(0x20, address=LIGHT)
        await self.rx(LIGHT, 0x11)
        await self.expect_state(light, "on")
        await self.clear_tx()
        await self.service(light, "toggle")
        await self.expect_tx(0x40, address=LIGHT)
        await self.rx(LIGHT, 0x10)
        await self.expect_state(light, "off")

        # Check both ends and the downward dimming path, not only 0 -> 50%.
        for brightness, command in ((255, 0x20), (64, 0x40)):
            await self.clear_tx()
            await self.service(light, "turn_on", brightness=brightness)
            await self.expect_tx(command, address=LIGHT)
            await self.expect_tx(0, address=LIGHT)
            await wait_for(
                lambda brightness=brightness: self.state(
                    light, lambda state: abs(state["attributes"].get("brightness", -100) - brightness) <= 5
                ),
                f"light brightness {brightness}",
            )
        await self.clear_tx()
        await self.service(light, "turn_on", brightness=0)
        await self.expect_tx(0x40, address=LIGHT)
        await self.rx(LIGHT, 0x10)
        await self.expect_state(light, "off")

    async def add_integration(self):
        flow = await self.http(
            "POST",
            "/api/config/config_entries/flow",
            {"handler": "mqtt" if self.mode == "mqtt" else "esphome", "show_advanced_options": False},
        )
        for _ in range(5):
            self.record("ha_config_flow", flow)
            if flow["type"] == "create_entry":
                return
            assert flow["type"] == "form", flow
            step = flow["step_id"]
            if self.mode == "mqtt" and step in ("user", "broker"):
                data = {
                    "broker": "127.0.0.1",
                    "port": 1883,
                    "protocol": "3.1.1",
                    "username": "",
                    "password": "",
                    "other_settings": {"set_client_cert": False, "set_ca_cert": "off", "transport": "tcp"},
                }
            elif self.mode == "mqtt" and step == "options":
                data = {}
            elif self.mode == "native" and step == "user":
                data = {"host": "127.0.0.1", "port": 16053}
            else:
                raise AssertionError(f"Unexpected HA config flow: {flow}")
            flow = await self.http("POST", f"/api/config/config_entries/flow/{flow['flow_id']}", data)
        raise AssertionError(f"Config flow did not complete: {flow}")

    async def start(self):
        setup_started = time.monotonic()
        for name in ("ha", "broker"):
            await command("docker", "rm", "-f", name, check=False)
        runtime = WORK / "runtime"
        if runtime.exists():
            shutil.rmtree(runtime)
        (runtime / "ha").mkdir(parents=True)
        (runtime / "ha" / "configuration.yaml").write_text(
            "homeassistant:\n  name: Elero Integration Test\n  latitude: 48\n  longitude: 16\n"
            "  elevation: 0\n  unit_system: metric\n  time_zone: Europe/Vienna\n"
            "http:\napi:\nwebsocket_api:\nconfig:\nonboarding:\nfrontend:\nlogger:\n  default: info\n"
        )
        (runtime / "mosquitto.conf").write_text(
            "listener 1883\nallow_anonymous true\npersistence false\nlog_type all\n"
        )
        binary = (WORK / "tests" / f"{self.mode}.bin").read_bytes()
        (runtime / "flash.bin").write_bytes(binary + b"\xff" * (4 * 1024 * 1024 - len(binary)))
        await command(
            "docker",
            "run",
            "-d",
            "--name",
            "broker",
            "--network",
            "host",
            "-v",
            f"{runtime}/mosquitto.conf:/mosquitto/config/mosquitto.conf:ro",
            "eclipse-mosquitto:2.0.22",
        )
        await self.launch_radio()
        await command(
            "docker", "run", "-d", "--name", "ha", "--network", "host", "-v", f"{runtime}/ha:/config", HA_IMAGE
        )
        await wait_for(self.radio_ready, "firmware test radio", timeout=240)
        await wait_for(lambda: self.http("GET", "/api/onboarding", auth=False), "HA onboarding", timeout=180)
        auth = await self.http(
            "POST",
            "/api/onboarding/users",
            {
                "name": "E2E",
                "username": "e2e",
                "password": "local-fixture-only",
                "client_id": "http://127.0.0.1:8123/",
                "language": "en",
            },
            auth=False,
        )
        tokens = await self.http(
            "POST",
            "/auth/token",
            auth=False,
            form={"grant_type": "authorization_code", "code": auth["auth_code"], "client_id": "http://127.0.0.1:8123/"},
        )
        self.token = tokens["access_token"]
        # Use HA's own onboarding handlers and configuration-entry flows.
        for step in ("core_config", "analytics"):
            await self.http("POST", f"/api/onboarding/{step}", {})
        result = await self.firmware({"type": "import_config", "snapshot": SNAPSHOT}, "import_result")
        assert result["added"] == len(DEVICES), result
        assert not result.get("errors"), result
        if self.mode == "native":
            # Native shells are constructed from NVS at boot.
            await self.restart_firmware()
        await self.add_integration()
        if self.mode == "mqtt":
            await self.wait_mqtt_commands(0, DEVICES, groups=True)

            async def light_state_subscriptions_ready():
                log = await command("docker", "logs", "broker")
                topics = [
                    f"elero/light/{int(device['dst_address'], 16):06x}/state (QoS "
                    for device in DEVICES
                    if device["device_type"] == "light"
                ]
                return all(topic in log for topic in topics)

            await wait_for(light_state_subscriptions_ready, "HA light state subscriptions")

        async def find_entities():
            states = await self.http("GET", "/api/states")
            for domain, name, key in (
                ("cover", "Test Blind", "cover"),
                ("light", "Test Light", "light"),
                ("light", "Group Light", "other_light"),
                ("cover", "Step Blind", "step"),
                ("cover", "Plain Blind", "plain"),
                ("cover", "Test Group", "group"),
            ):
                matches = [
                    state
                    for state in states
                    if state["entity_id"].startswith(domain + ".")
                    and name.lower() in state["attributes"].get("friendly_name", "").lower()
                ]
                if len(matches) != 1:
                    return False
                self.entities[key] = matches[0]["entity_id"]
                self.initial_states[key] = matches[0]
            self.record("discovered_entities", states)
            return True

        await wait_for(find_entities, "all HA entities", timeout=180)
        await self.connect_events()
        await self.seed_receivers()
        self.record("timing", {"phase": "fixture_start", "seconds": round(time.monotonic() - setup_started, 3)})

    async def launch_radio(self):
        for name, command_line in (
            ("model", ["python3", "-u", "/workspace/tests/espemu_radio.py"]),
            (
                "firmware",
                [
                    "/workspace/esp-emu",
                    "--chip",
                    "esp32s3",
                    "--firmware",
                    "/workspace/runtime/flash.bin",
                    "--save-state",
                    "--net",
                    "user,hostfwd=tcp:127.0.0.1:18080-:80,hostfwd=tcp:127.0.0.1:16053-:6053",
                    "--cosim-tcp",
                    "127.0.0.1:28590",
                    "--control-tcp",
                    "127.0.0.1:28591",
                ],
            ),
        ):
            log = (self.path / f"{name}.log").open("a")
            self.log_files.append(log)
            process = await asyncio.create_subprocess_exec(
                *command_line, stdout=log, stderr=log, env={**os.environ, "ELERO_EVIDENCE": str(self.path)}
            )
            self.processes.append(process)

    async def restart_firmware(self, *, reload_api=True):
        restart_started = time.monotonic()
        log_offset = (self.path / "firmware.log").stat().st_size
        cursor = len((await command("docker", "logs", "broker")).splitlines()) if self.mode == "mqtt" else 0
        # Preferences writes are buffered by ESPHome. A hard reset before the
        # configured write interval is a power-loss test, not a normal reboot.
        started = (await self.radio("ping"))["sim_ns"]

        async def flushed_interval():
            return (await self.radio("ping"))["sim_ns"] - started >= 2_000_000_000

        await wait_for(flushed_interval, "ESPHome preference write interval", timeout=60)
        # The emulator's hard reset preserves the emulated flash and exercises
        # the ROM, bootloader, NVS restore and adapter construction again.
        connection = socket.create_connection(("127.0.0.1", 28591), timeout=5)
        with connection:
            connection.sendall(b"reset\n")
            assert connection.recv(128).strip() == b"ok"
        await asyncio.sleep(1)
        await wait_for(self.radio_ready, "firmware after restart", timeout=240)
        snapshot = await wait_for(
            lambda: self.firmware({"type": "export_config"}, "config_snapshot"),
            "firmware web server after reset",
            timeout=120,
        )
        if self.mode == "mqtt":
            await self.wait_mqtt_commands(cursor, snapshot["devices"], groups=True)
        elif self.entities:
            if reload_api:
                entries = await self.ha_ws("config_entries/get")
                entry = next(e for e in entries if e["domain"] == "esphome")
                await self.http("POST", f"/api/config/config_entries/entry/{entry['entry_id']}/reload")

            async def reconnected():
                with (self.path / "firmware.log").open() as log:
                    log.seek(log_offset)
                    return any("Home Assistant " in line and ": connected" in line for line in log)

            # A chip reset can leave HA's old TCP connection apparently alive.
            # Cached HA states alone do not establish a new API session.
            await wait_for(reconnected, "new Home Assistant API session after reset", timeout=150)
        self.record(
            "timing",
            {
                "phase": "firmware_restart",
                "seconds": round(time.monotonic() - restart_started, 3),
                "api_reload": self.mode == "native" and bool(self.entities) and reload_api,
            },
        )

    async def test_discovery(self):
        if self.mode == "mqtt":
            assert self.initial_states["cover"]["state"] in ("unavailable", "unknown"), self.initial_states["cover"]
        for address in (COVER, STEP, PLAIN):
            await self.rx(address, 2)
        await self.rx(LIGHT, 0x10)
        await self.expect_state(self.entities["cover"], "closed")
        await self.expect_state(self.entities["light"], "off")
        calibrated = await self.state(self.entities["cover"])
        untimed = await self.state(self.entities["step"])
        # HA CoverEntityFeature.SET_POSITION = 4, SUPPORT_TILT = 16.
        assert calibrated["attributes"]["supported_features"] & 4, calibrated
        assert calibrated["attributes"]["supported_features"] & 16, calibrated
        assert not untimed["attributes"]["supported_features"] & 4, untimed

    async def test_cover_round_trip(self):
        entity = self.entities["cover"]
        for service, cmd, feedback, final in (("open_cover", 0x20, 1, "open"), ("close_cover", 0x40, 2, "closed")):
            await self.clear_tx()
            await self.service(entity, service)
            await self.expect_tx(cmd, address=COVER)
            # HA's MQTT cover is optimistic. A contradictory receiver report
            # must override the requested state, or a service echo could pass.
            opposite_feedback = 2 if feedback == 1 else 1
            opposite_state = "closed" if feedback == 1 else "open"
            await wait_for(
                lambda opposite_state=opposite_state: self.state(
                    entity, lambda state: state["state"] != opposite_state
                ),
                "command changes cover state before receiver feedback",
            )
            await self.rx_state(COVER, opposite_feedback, entity, opposite_state)
            await self.rx_state(COVER, feedback, entity, final)
        await self.clear_tx()
        await self.service(entity, "open_cover")
        await self.expect_tx(0x20, address=COVER)
        await self.service(entity, "stop_cover")
        await self.expect_tx(0x10, address=COVER, packet_type=0x6A)
        await self.rx(COVER, 0x0D)
        await wait_for(
            lambda: self.state(entity, lambda state: state["state"] in ("open", "closed")),
            "cover is idle after STOPPED report",
        )

    async def test_external_status(self):
        await self.rx(COVER, 1)
        await self.expect_state(self.entities["cover"], "open")
        await self.rx(COVER, 2)
        await self.expect_state(self.entities["cover"], "closed")
        await self.rx(LIGHT, 0x11)
        await self.expect_state(self.entities["light"], "on")

    async def test_light_round_trip(self):
        for service, cmd, feedback, final in (("turn_on", 0x20, 0x11, "on"), ("turn_off", 0x40, 0x10, "off")):
            await self.clear_tx()
            await self.service(self.entities["light"], service)
            await self.expect_tx(cmd, address=LIGHT)
            entity = self.entities["light"]
            await self.expect_state(entity, final)
            opposite, opposite_state = (0x10, "off") if final == "on" else (0x11, "on")
            await self.rx_state(LIGHT, opposite, entity, opposite_state)
            await self.rx_state(LIGHT, feedback, entity, final)

    async def test_light_brightness(self):
        await self.rx(LIGHT, 0x10)
        await self.expect_state(self.entities["light"], "off")
        await self.clear_tx()
        await self.service(self.entities["light"], "turn_on", brightness=128)
        await self.expect_tx(0x20, address=LIGHT)
        await self.expect_tx(0, address=LIGHT)

        async def reached():
            return await self.state(
                self.entities["light"],
                lambda state: state["state"] == "on" and abs(state["attributes"].get("brightness", -100) - 128) <= 5,
            )

        self.record("ha_asserted_brightness", await wait_for(reached, "50 percent light brightness"))

    async def test_step_and_tilt(self):
        await self.clear_tx()
        await self.service(self.entities["step"], "open_cover")
        await self.expect_tx(0x21, address=STEP, packet_type=0x6A)
        await self.rx(STEP, 1)
        await self.expect_state(self.entities["step"], "open")
        await self.clear_tx()
        await self.service(self.entities["step"], "set_cover_tilt_position", tilt_position=0)
        await self.expect_tx(0x40, address=STEP)
        await self.expect_tx(0, address=STEP)
        await self.clear_tx()
        await self.service(self.entities["cover"], "set_cover_tilt_position", tilt_position=100)
        await self.expect_tx(0x24, address=COVER)
        await self.rx(COVER, 4)
        await wait_for(
            lambda: self.state(
                self.entities["cover"], lambda state: state["attributes"].get("current_tilt_position") == 100
            ),
            "HA tilt position after receiver report",
        )

    async def test_position(self):
        for target in (50,):
            await self.rx(COVER, 2)
            await self.expect_state(self.entities["cover"], "closed")
            await self.clear_tx()
            await self.service(self.entities["cover"], "set_cover_position", position=target)
            await self.expect_tx(0x20, address=COVER)
            await self.expect_tx(0x10, address=COVER, packet_type=0x6A)

            async def reached(target=target):
                return await self.state(
                    self.entities["cover"],
                    lambda state: abs(state["attributes"].get("current_position", -100) - target) <= 3,
                )

            self.record("ha_asserted_position", await wait_for(reached, f"position {target}"))

    async def test_invalid_input(self):
        await self.rx(COVER, 2)
        await self.expect_state(self.entities["cover"], "closed")
        await self.rx(COVER, 1, crc_ok=False)
        await self.radio("rx", frame=[255, 1, 2])
        await asyncio.sleep(2)
        assert (await self.state(self.entities["cover"]))["state"] == "closed"
        before = await self.firmware({"type": "export_config"}, "config_snapshot")
        result = await self.firmware({"type": "import_config", "snapshot": {"snapshot_version": 999}}, "error")
        assert "snapshot_version" in result["msg"], result
        snapshot = await self.firmware({"type": "export_config"}, "config_snapshot")
        assert {k: v for k, v in snapshot.items() if k != "exported_at"} == {
            k: v for k, v in before.items() if k != "exported_at"
        }, (before, snapshot)
        await self.rx(COVER, 1)
        await self.expect_state(self.entities["cover"], "open")

    async def test_group(self):
        entity = self.entities["group"]
        for service, cmd, packet_type, feedback, state in (
            ("open_cover", 0x20, 0x44, 1, "open"),
            ("close_cover", 0x40, 0x44, 2, "closed"),
            ("open_cover", 0x20, 0x44, 1, "open"),
            ("stop_cover", 0x10, 0x6A, 0x0D, "open"),
        ):
            await self.clear_tx()
            await self.service(entity, service)
            if packet_type == 0x44:
                await self.expect_tx(cmd, destinations=[1, 4], channel=0)
            else:
                reached = set()

                async def stopped(reached=reached):
                    for frame in (await self.radio("tx"))["frames"]:
                        parsed = decode(frame)
                        if parsed["command"] != 0x10:
                            continue
                        assert parsed["type"] == 0x6A and parsed["src"] == int(CONTROLLER, 16), parsed
                        assert parsed["destinations"] in ([COVER], [PLAIN]), parsed
                        address = parsed["destinations"][0]
                        assert parsed["channel"] == address - COVER + 1, parsed
                        reached.add(address)
                        self.record("asserted_group_stop_tx", parsed)
                    return reached == {COVER, PLAIN}

                await wait_for(stopped, "individual STOP for both group members", timeout=45)
            await self.rx(COVER, feedback)
            await self.rx(PLAIN, feedback)
            await self.expect_state(entity, state)

    async def test_tx_failure_recovery(self):
        await self.clear_tx()
        before = (await self.radio("ping"))["failed_strobes"]
        await self.radio("fail_tx", value=True)
        try:
            await self.service(self.entities["plain"], "open_cover")

            async def failed():
                return (await self.radio("ping"))["failed_strobes"] > before

            await wait_for(failed, "production driver sees STX failure", timeout=45)
        finally:
            await self.radio("fail_tx", value=False)
        await self.service(self.entities["plain"], "close_cover")
        await self.expect_tx(0x40, address=PLAIN)
        await self.rx(PLAIN, 2)
        await self.expect_state(self.entities["plain"], "closed")

    async def test_backup_restore(self):
        before = await self.firmware({"type": "export_config"}, "config_snapshot")
        for entry in before["devices"]:
            await self.firmware(
                {"type": "remove_device", "dst_address": entry["dst_address"], "device_type": entry["device_type"]},
                "device_removed",
            )
        empty = await self.firmware({"type": "export_config"}, "config_snapshot")
        assert empty["devices"] == [], empty
        restored = await self.firmware({"type": "import_config", "snapshot": before}, "import_result")
        assert restored["added"] == len(DEVICES) and not restored["errors"], restored
        if self.mode == "native":
            await self.restart_firmware()
        after = await self.firmware({"type": "export_config"}, "config_snapshot")
        assert after["devices"] == before["devices"]
        assert after["groups"] == before["groups"]
        await self.rx(COVER, 2)
        await self.expect_state(self.entities["cover"], "closed")

    async def test_configuration_update(self):
        changed = {**DEVICES[0], "open_duration_ms": 0, "close_duration_ms": 0, "supports_tilt": False}
        for configuration, supports_position in ((changed, False), (DEVICES[0], True)):
            result = await self.firmware(
                {"type": "import_config", "snapshot": {"snapshot_version": 2, "devices": [configuration]}},
                "import_result",
            )
            assert result["updated"] == 1 and not result["errors"], result
            if self.mode == "native":
                await self.restart_firmware()
            await self.rx(COVER, 2)

            async def configured(supports_position=supports_position):
                state = await self.state(self.entities["cover"])
                if state is None:
                    return None
                features = state["attributes"]["supported_features"]
                return (
                    state
                    if bool(features & 4) == supports_position and bool(features & 16) == supports_position
                    else None
                )

            self.record("ha_asserted_capabilities", await wait_for(configured, "updated cover capabilities"))

    async def test_problem_states(self):
        states = await self.http("GET", "/api/states")
        matches = [
            state["entity_id"]
            for state in states
            if state["entity_id"].startswith("binary_sensor.")
            and state["attributes"].get("friendly_name", "").lower() == "test blind problem"
        ]
        assert len(matches) == 1, matches
        for value, problem in ((5, "blocking"), (6, "overheated"), (2, "none")):
            await self.rx(COVER, value)
            await self.expect_state(matches[0], "off" if problem == "none" else "on")

            async def reported(problem=problem):
                return await self.state(
                    self.entities["cover"], lambda state: state["attributes"].get("problem_type") == problem
                )

            self.record("ha_asserted_problem", await wait_for(reported, f"problem {problem}"))

    async def test_persistence(self):
        identities = await self.wait_inventory()
        before = await self.firmware({"type": "export_config"}, "config_snapshot")
        await self.restart_firmware(reload_api=False)
        after = await self.firmware({"type": "export_config"}, "config_snapshot")
        assert before["devices"] == after["devices"]
        assert before["groups"] == after["groups"]
        await self.rx(COVER, 2)
        await self.expect_state(self.entities["cover"], "closed")
        assert await self.wait_inventory() == identities
        # Also prove commands still reach firmware after reconnection.
        await self.clear_tx()
        await self.service(self.entities["cover"], "open_cover")
        await self.expect_tx(0x20, address=COVER)
        await self.rx(COVER, 1)
        await self.expect_state(self.entities["cover"], "open")

    async def test_ha_restart(self):
        await self.rx(COVER, 2)
        await self.expect_state(self.entities["cover"], "closed")
        assert self.event_task is not None
        self.event_task.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await self.event_task
        await command("docker", "restart", "ha")
        await wait_for(lambda: self.http("GET", "/api/"), "HA restart", timeout=180)
        await self.expect_state(self.entities["cover"], "closed")
        await self.connect_events()
        if hasattr(self, "identity_baseline"):
            assert await self.wait_inventory() == self.identity_baseline
        await self.clear_tx()
        await self.service(self.entities["cover"], "open_cover")
        await self.expect_tx(0x20, address=COVER)
        await self.rx(COVER, 2)
        await self.expect_state(self.entities["cover"], "closed")
        await self.rx_state(COVER, 1, self.entities["cover"], "open")

    async def test_broker_restart(self):
        await self.rx(COVER, 2)
        await self.expect_state(self.entities["cover"], "closed")
        cursor = len((await command("docker", "logs", "broker")).splitlines())
        await command("docker", "stop", "broker")
        await self.expect_state(self.entities["cover"], "unavailable")
        await command("docker", "start", "broker")
        # The broker has no persistence, so discovery and state must be replayed.

        async def subscribed():
            lines = (await command("docker", "logs", "broker")).splitlines()[cursor:]
            topic = f"elero/cover/{COVER:06x}/set (QoS 0)"
            for index, line in enumerate(lines):
                if topic in line and any("Sending SUBACK to elero-e2e-" in ack for ack in lines[index + 1 : index + 4]):
                    self.record("broker_subscription_restored", {"topic": topic})
                    return True
            return False

        # An online availability message can precede adapter subscriptions.
        # Wait for the actual broker acknowledgement before issuing a command.
        await wait_for(subscribed, "firmware command subscription restored", timeout=90)
        await self.expect_state(self.entities["cover"], "closed")
        await self.clear_tx()
        await self.service(self.entities["cover"], "open_cover")
        await self.expect_tx(0x20, address=COVER)
        await self.rx(COVER, 1)
        await self.expect_state(self.entities["cover"], "open")

    async def close(self):
        if self.event_task:
            self.event_task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self.event_task
        for process in reversed(self.processes):
            if process.returncode is None:
                process.send_signal(signal.SIGINT)
                try:
                    await asyncio.wait_for(process.wait(), 20)
                except TimeoutError:
                    process.kill()
                    await process.wait()
        for log in self.log_files:
            log.close()
        for name in ("ha", "broker"):
            text = await command("docker", "logs", name, check=False)
            (self.path / f"{name}.log").write_text(text)
            (self.path / f"{name}-{self.case}.log").write_text(text)
        (self.path / "containers.json").write_text(await command("docker", "inspect", "ha", "broker", check=False))


async def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=["all", "mqtt", "native"], default="all")
    parser.add_argument("--test")
    args = parser.parse_args()
    common_names = [name.removeprefix("test_") for name in vars(Suite) if name.startswith("test_")]
    available = [
        name for name in common_names if args.mode != "native" or name not in ("problem_states", "broker_restart")
    ]
    if args.test and not any(args.test in name for name in available):
        parser.error(f"No scenarios match {args.test!r}")
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    report = ET.Element("testsuite", name="homeassistant-integration")
    failures = 0
    modes = ("mqtt", "native") if args.mode == "all" else (args.mode,)
    async with aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=15)) as session:
        for mode in modes:
            suite = Suite(mode, session)
            names = [
                "discovery",
                "entity_inventory",
                "integration_readd",
                "action_buttons",
                "cover_round_trip",
                "external_status",
                "light_round_trip",
                "light_brightness",
                "step_and_tilt",
                "position",
                "invalid_input",
                "group",
                "group_position",
                "light_group",
                "tx_failure_recovery",
                "persistence",
                "backup_restore",
                "configuration_update",
                "ha_restart",
                "entity_lifecycle",
            ]
            if mode == "mqtt":
                names.append("problem_states")
                names.append("broker_restart")
            names.extend(["cover_actions", "light_actions", "rename"])
            try:
                print(f"[{mode}] Start real HA and ESP32 firmware", flush=True)
                await suite.start()
                names = [name for name in names if not args.test or args.test in name]
                reset_fixture = False
                for name in names:
                    if reset_fixture:
                        await suite.close()
                        suite = Suite(mode, session)
                        await suite.start()
                        reset_fixture = False
                    suite.case = name
                    suite.clean_after_failure = False
                    case = ET.SubElement(report, "testcase", classname=mode, name=name)
                    started = time.monotonic()
                    try:
                        await getattr(suite, "test_" + name)()
                        print(f"[{mode}] PASS {name}", flush=True)
                    except Exception:
                        failures += 1
                        error = traceback.format_exc()
                        ET.SubElement(case, "failure").text = error
                        print(f"[{mode}] FAIL {name}\n{error}", flush=True)
                        # Rebuild HA and flash before continuing after a failure.
                        reset_fixture = not suite.clean_after_failure
                    finally:
                        case.set("time", str(round(time.monotonic() - started, 3)))
            except Exception:
                failures += 1
                error = traceback.format_exc()
                case = ET.SubElement(report, "testcase", classname=mode, name="startup")
                ET.SubElement(case, "failure").text = error
                print(f"[{mode}] FAIL startup\n{error}", flush=True)
            finally:
                await suite.close()
                report.set("tests", str(len(report)))
                report.set("failures", str(failures))
                report.set("skipped", str(sum(case.find("skipped") is not None for case in report)))
                ET.ElementTree(report).write(EVIDENCE / "results.xml", encoding="unicode")
                summary = [
                    {
                        "mode": case.get("classname"),
                        "test": case.get("name"),
                        "passed": case.find("failure") is None and case.find("skipped") is None,
                        "skipped": case.find("skipped") is not None,
                        "seconds": case.get("time"),
                    }
                    for case in report
                ]
                (EVIDENCE / "results.json").write_text(json.dumps(summary, indent=2))
    return int(failures != 0)


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
