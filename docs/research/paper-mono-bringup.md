# PaperMono USB bring-up spike

Date: 2026-09-22. Branch: `pi/paper-mono-spike`.

Worktree: `/private/tmp/esphome-elero-paper-mono`.
Starting commit: `c0499e3` (the active branch's committed Elero core).
Comparison baseline on `main`: `6bfde13db67987362fa669783f99b991f9d47699`.
The original checkout's uncommitted application changes were not copied or modified.
The pinned mise tool configuration was copied so builds use installed managed runtimes.

## Hardware

- USB port: `/dev/cu.usbmodem101` (may change after reconnect).
- ESP32-S3 revision 0.2, MAC `28:84:85:44:71:c0`.
- Flash probe: 16 MB, quad flash, 3.3 V.
- Embedded PSRAM: 8 MB.
- Original full flash backup: `/private/tmp/paper-mono-original-2884854471c0.bin` (16,777,216 bytes).

## Scope

The channel TX bench image described below has since been expanded into the
[native management frontend](paper-mono-native-ui.md). That document describes the
current screens, NVS persistence, backup support, and validation limits. The evidence
below records the earlier RX/TX bring-up stages.

`configs/config.paper-mono-spike.yaml` is an ESP-IDF image with USB logging and a native
480×800 e-paper reception screen.
It has no network credentials, web server, provisioned motors, or automatic TX.
The initial reception screen has been replaced with a channel TX bench screen:
channels 1–15, five per page, each with Up / Stop / Down controls. At startup it captures
the source address from the first CRC-valid Up/Down/Stop command and keeps it fixed.
"Sniff remote" explicitly clears the selection and arms capture again. The address is
not persisted across reboot. No motor destination address or NVS device is required.

Touch commands reuse `CommandSender` and the RF task: single-channel button envelope,
three repeats, existing completion/retry handling. STOP supersedes pending repeats.
Page/address transitions gate controls until the new frame has finished refreshing;
commands remain available during status-only refreshes. TX occurs only after a tap.
USB completion logs distinguish RF transmission success from motor response.

`paper_mono_spike` directly implements the board's PM1 power and IOE1 reset setup.
It follows factory firmware antenna control: IOE1 PYG2 held high and SX1262 DIO2 enabled.
Board labels PYG1..PYG14 use zero-based register bits 0..13; PYG10 is bit 1 in the high bank.
The charger I2C connection is disabled using PYG11. Radio regulator is LDO; TCXO is 3.0 V.

This is intentionally a hardware experiment, not a general M5PM1/M5IOE1 implementation.
Radio reset, display reset, and touch transactions share a mutex, including complete
read-modify-write sequences. Other runtime I2C clients must use the same ownership
mechanism before being added.

`elero_paper_spike` registers a thin `OutputAdapter` with the existing registry. Events
mark the display dirty; rendering reads the registry. No core-to-screen call or new
event abstraction was required. ESPHome drives the display loop independently of the
adapter loop. Radio and panel use separate hardware SPI buses.

Panel sequencing follows M5Stack's OTP sample: a full refresh establishes the baseline,
then partial refreshes, with another full refresh after nine partials. Busy waits are
cooperative, SPI uploads use 2 KB chunks, and incoming events coalesce while a frozen
frame is being refreshed. Frame composition is split across loop iterations. Touch is
polled every 40 ms. The framebuffer uses 48 KB.

Core changes: permit `GPIOPin` for SX1262 reset; add `regulator_ldo` configuration,
defaulting to false to preserve existing boards. Protocol encoding/framing is unchanged.

## Commands

Run in the worktree:

```sh
mise exec -- uv sync --python 3.12.14
mise exec -- uv run esphome compile configs/config.paper-mono-spike.yaml
mise exec -- uv run esphome upload configs/config.paper-mono-spike.yaml --device /dev/cu.usbmodem101
mise exec -- uv run esphome logs configs/config.paper-mono-spike.yaml --device /dev/cu.usbmodem101
```

ESPHome is the branch's pinned 2026.2.4, using its ESP-IDF 5.5.2 framework dependencies.
The original working directory's ESPHome 2026.9 compatibility work is separate.

## Validation so far

- ESPHome configuration/code generation: passed after correcting output-only pin mode validation.
- ESP32-S3 firmware compilation: passed.
- USB upload: passed, written data hashes verified.
- Startup: verified after a physical reset restored USB access and the heartbeat image was uploaded.
  The precise cause of the first silent reset is not established.
- Runtime: repeated 5-second heartbeats show `board_failed=0`, `hub_failed=0`, `radio_mode=0` (RX).
  SX1262 health reads show chip mode `0x5` (RX), IRQ `0x0000`, and device errors `0x0000`.
- Over-the-air RX verified after the user pressed a nearby Elero remote: repeated decoded
  packets from `0x4fca30` reached registry publication at 13:47:51–13:47:54 local time.
  The SX1262 path validates software CRC before `decode_fifo` can accept a packet.
  Subsequent health checks remained in RX with no reported device errors.
- Channel TX radio completion verified: remote `4FCA30`, channel 1, Down / Stop / Up
  at 14:15:27–14:15:32. All three repeats per command reported `TX_DONE` and successful
  completion; each hardware TX took about 9 ms. Up was tapped during panel refresh
  and completed before that refresh ended. Radio returned to RX with zero device errors.
  The user confirmed the physical motor controls work ("magic. it works."). This
  validates end-to-end channel control with the sniffed remote address on this setup.
- Native display/touch image: compiled and uploaded successfully; post-flash radio
  health and heartbeat remain normal. Partial refresh cycles complete in 947–951 ms,
  with frame composition taking about 34 ms. A packet was decoded at 13:58:20.998,
  during the refresh that started at 13:58:20.927 and completed at 13:58:21.841.
  The user confirmed upright, readable output and tapped the screen. USB logs captured
  touch both while idle and during a refresh (`panel_busy=1`).
- Rendering initially triggered a 51 ms ESPHome loop warning. Splitting composition
  into five parts reduced observed drawing slices to at most 19 ms with three remotes.
  The updated firmware was compiled and flashed: initial full refresh took 4.732 s,
  subsequent partial cycles took 909–941 ms. RX continued during refresh, with no
  component latency warnings in the updated capture so far.
- Existing Python tests: 36 passed.
- New board Python module Ruff lint and formatting: passed.
- Full-tree Ruff: existing import ordering in two test files; formatting differences in four existing files.
- Full-tree ty: unresolved dynamic external-component/test imports in four existing locations.
- Existing C++ suite build: blocked by its radio mock lacking ESPHome's `GPIOPin` base type.
  No test files or mocks were changed, in accordance with the project instruction.
- Remaining compiled C++ test executables: 468 tests passed; this does not validate the unbuilt SX1262 host target.

Logs are saved as `/private/tmp/paper-mono-{build,flash,boot,physical-reset,cmake,pytest,ruff,format,ty}.log`.
Successful runtime capture: `/private/tmp/paper-mono-heartbeat-serial.log`.
Heartbeat image upload: `/private/tmp/paper-mono-flash-heartbeat.log`.
Display image build/upload/runtime: `/private/tmp/paper-mono-display-{build,flash,serial}.log`.
Final rendering fix upload/runtime: `/private/tmp/paper-mono-display-final-{flash,serial}.log`.
Channel TX screen build/upload/runtime: `/private/tmp/paper-mono-tx-{build,flash,serial}.log`.

RX evidence from the USB capture:

```text
[13:47:51.756][D][elero:121][elero_rf]: decode_fifo: 1 pkt(s), 32 bytes, 91us
[13:47:51.758][D][elero.registry:1075]: 0x4fca30: publish [RSSI|REMOTE] (0x0120)
[13:47:54.400][D][elero:121][elero_rf]: decode_fifo: 1 pkt(s), 32 bytes, 77us
[13:47:54.402][D][elero.registry:1075]: 0x4fca30: publish [RSSI|REMOTE] (0x0120)
[13:47:54.569][I][paper_mono_spike:062]: alive uptime=171s board_failed=0 hub_failed=0 radio_mode=0
[13:47:57.762][D][elero.sx1262:348][elero_rf]: health: mode=0x5 irq=0x0000 err=0x0000
```

This validates reception and registry dispatch on this board, including reception during
a panel refresh cycle, and end-to-end channel transmission with a user-confirmed motor
response. RF range and packet-loss rate remain unmeasured.

## Sources

- [Factory radio HAL](https://github.com/m5stack/M5PaperMono-UserDemo/blob/c1099107271d31a0678d661a896e2b04dbb331ea/main/hal/hal_lora.cpp).
- [M5IOE1 definitions](https://github.com/m5stack/M5IOE1/tree/main/src).
- [M5Unified board power implementation](https://github.com/m5stack/M5Unified/blob/master/src/utility/Power_Class.cpp).
- [PaperMono official specifications](https://docs.m5stack.com/en/core/PaperMono).
- [UI options research](paper-mono-ui-options.md).
- [Official OTP panel sample](https://github.com/m5stack/M5PaperMono-OTP-Demo/tree/c7c02554f89fd06f80d988b805b2a59050c78a46).
