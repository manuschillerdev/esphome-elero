# Home Assistant integration tests

The suite builds the ESPHome firmware in this checkout. It boots the firmware on an emulated ESP32-S3 in a disposable Linux VM. Home Assistant connects to the gateway through either the ESPHome native API or MQTT. The MQTT tests use Mosquitto.

## Run the suite

Use a Linux host with KVM, smolvm 1.33.0, and the project's Python and uv versions. Allow network access for the first build and image downloads. The VM uses four CPUs, 6 GiB of RAM and a 30 GiB virtual disk. Docker runs inside the VM.

```sh
uv sync --locked
uv run --locked python tests/integration/run.py
```

To select a mode or a scenario, use these commands.

```sh
uv run --locked python tests/integration/run.py --mode native --test backup_restore
uv run --locked python tests/integration/run.py --mode mqtt --test broker_restart
```

Use `--keep` to inspect a failed run. To run again in that disposable VM, pass `--reuse --name elero-e2e-NAME`. This resets the test services and evidence inside the VM. Use `--skip-build` only when testing an existing local firmware image. The manifest records the image hash and this option.

The GitHub Actions workflow runs the two modes in separate jobs on relevant pull requests. It also supports manual runs. Each job uploads the scenario results and build manifest. [Workflow](../../.github/workflows/ha-integration.yml)

## What a result covers

A Home Assistant service call passes through the selected adapter, the registry, the command sender and the CC1101 driver. The emulator runs the ESP32 SPI controller. A separate model records the bytes written to the radio transmit buffer. Tests check the command, destination, channel and packet type. Scripted receiver replies go into the radio receive buffer. The tests read Home Assistant state and listen for state changes. The cover and light tests also send a reply that contradicts the command.

| Area | Checks |
| --- | --- |
| Home Assistant setup | Integration setup, removal and re-addition. Entity and device inventory, capabilities and unique IDs. |
| Commands and feedback | Cover and light control, brightness, tilt, steps, position, buttons and groups. |
| Device changes | Add, disable, enable, rename and remove. Check the entities and send commands after the changes. |
| Backup and persistence | Export the settings, remove the devices, import the saved snapshot and compare devices and groups. Check receiver feedback in Home Assistant. Reset the emulated chip in a separate persistence test. |
| Recovery | Restart Home Assistant, restart the MQTT broker, fail a radio transmit request and check the next command. |
| Input checks | Invalid messages, malformed receiver frames and invalid settings. |

Each run writes to `.tmp/integration/<run-id>/`. Start with `scenarios.log` and `evidence/results.xml`. The manifest records the revision, source file hashes and firmware hashes. The proof log records service calls and Home Assistant events. The SPI log records the radio model's observations. Detailed VM logs stay local.

## Coverage limits

- The radio model covers the CC1101 registers and buffers used by this suite. Scripted replies are independent of commands. The suite cannot establish radio range, receiver acceptance or physical position.
- The Home Assistant tests use ESP32-S3, ESP-IDF, Home Assistant 2026.11.0 and Mosquitto 2.0.22. Other Home Assistant versions, Arduino, SX1262 and SX1276 need separate coverage.
- The backup test uses the firmware WebSocket API. Browser file download and upload, restore from an older snapshot, and recovery on replacement hardware need separate checks.
- A chip reset preserves the emulator process. The suite does not cover interrupted flash writes, a new emulator process with the saved flash, or switching a gateway between native API and MQTT.
- The tests use one gateway. They do not exercise two gateways on the same broker or simultaneous control from two Home Assistant instances.

## Emulator probe

The separate ESP32-S3 and ESP32-C6 probe configurations check SPI and a GPIO interrupt. Build a probe with `uv run --locked esphome compile tests/integration/espemu.yaml` or `espemu-c6.yaml`. The main suite uses the ESP32-S3. The test runner verifies the emulator archive checksum before use. [Emulator release](https://github.com/espressif/esp-emulator/releases/tag/v1.0.0-rc2)
