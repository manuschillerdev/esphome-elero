# PaperMono native frontend

> Historical development record. For the current component names, ownership,
> configuration and validation status, see [PaperMono integration](../PAPER_MONO.md).

Date: 2026-09-22. Worktree `/private/tmp/esphome-elero-paper-mono`, branch
`pi/paper-mono-spike`. Firmware configuration: `configs/config.paper-mono.yaml`.

## Navigation and feature mapping

The top bar always offers Channels, Devices, and Menu. Screens use large buttons,
five-row lists, previous/next controls, and paged settings. There is no animated
scrolling. The UI uses the existing ESPHome drawing/font layer and vendor OTP panel
sequence, without a browser, network credentials, or web server.

- **Channels:** channels 1–15, five per page, Up/Stop/Down. Sniff an existing physical
  remote or select a discovered remote from its device detail page. The selected
  source address is now persisted through ESPHome preferences.
- **Devices:** discovered and saved covers, lights, and remotes. Name/address search;
  type, saved/unsaved, and remote filters; sort by name, address, or signal.
- **Device detail:** projected state, raw RF state, signal, source/channel, save and
  active status. Cover and light controls, status check, tilt when configured,
  per-device packet history, selection for bulk actions, editing, and deletion.
  Remote entries have their own channel selector and Up/Stop/Down controls; their
  addresses remain classified as remote sources. Targeted command packets also
  create unsaved receiver candidates through `DeviceRegistry::discover_receiver`.
  Status-only traffic is not used to infer a receiver channel.
- **Device settings:** name, active flag, source/channel, travel times, tilt, dim
  time, and manual creation. Stable address/type cannot be changed on an existing
  record. Edits remain a draft until Save to NVS. Text/numeric keyboards have large
  keys, Delete and Clear, and support ASCII upper/lowercase, digits, spaces, and basic punctuation.
- **Groups:** control, create, rename, choose members, save, delete. Validation and
  command dispatch use the registry. Bulk commands and creating a group from selected
  devices are available from Devices → Bulk. Group membership uses stable addresses.
- **Hub:** name, version, frequency, counts, NVS status, check-all, restart.
  Frequency is informational, matching the web frontend's unimplemented runtime setter.
- **Learn-in:** source, channel, timeout, start, confirm up/down, cancel, core state.
  The default source derives from the board MAC when no physical remote is selected.
- **Simulate remote:** explicit source/destination/channel, command, envelope, type2,
  hop and payload inputs. Transmit confirmation; uses the existing raw TX queue API.
- **RF packets:** bounded 50-packet history; frozen browsing snapshots; address,
  channel, type, command, state filters; newest/signal ordering; refresh and clear.
  Details expose RF metadata; Replay opens the raw form for review before transmitting.
- **Backup/restore:** microSD JSON files compatible with the web UI's snapshot v1/v2.
  The parser, exporter and merge importer are shared in `elero_config/config_snapshot.*`.
  Export uses a fresh random filename. Restore confirms the chosen file, validates
  records, merges through registry upserts, and reports restored/skipped counts;
  detailed import errors are logged over USB. It does not erase unrelated devices.
  Files are bounded at 128 KB and the picker at 64 entries. The card is never formatted.

## Refresh and input behaviour

The UI builds an immutable frame containing labels and actions. Each render iteration
paints one element; SPI transfer remains chunked, and panel BUSY waits remain cooperative.
After panel completion, the frame's hit targets become active. Actions carry stable
IDs rather than list indexes or device pointers. Navigation gates taps until the
new frame is visible; Stop can still target the previously displayed device/channel.
Status-only refreshes do not block controls. Keyboard input can accumulate while text
refreshes. A minimum one-second idle gap coalesces redraws. Full cleanup follows nine
partial refreshes, as in the original spike.

Normal device commands use `DeviceRegistry`; channel emulation uses the already
bench-validated `CommandSender`; learn-in uses `LearnInManager`. No second device
state machine was added. Scene, drafts, selection, filters, and bounded packet history
are presentation state. Device persistence and group validation remain in the core.
Saved devices use the core's normal restore checks and polling. Enabling this frontend
sets NVS persistence without adding a Home Assistant output adapter.

## Control icons

Movement controls use bold arrows and a stop square; lights use a bulb and a
crossed-out bulb. Paging uses chevrons, edit/rename uses a pencil, and device/group
deletion uses a trash can. Navigation keeps its short labels under smaller icons.
The renderer draws these shapes directly into the existing monochrome framebuffer,
without another font or library. Button hit areas and command bindings are unchanged.
Keyboard labels (including Clear), unfamiliar operations, and confirmations remain
textual. Channel naming and persistent configuration are outside this change.

## Validation

- Expanded native ESP-IDF firmware compiles and uploads over USB.
- Existing web + NVS firmware configuration compiles after the shared codec extraction.
  The existing generated gzip web header from the original checkout was used as the
  compile asset; browser source was not changed or rebuilt. Existing Mongoose directory
  stub linker warnings remain in that configuration, unrelated to the codec extraction.
- Existing Python tests: 36 passed. New component Python lint, format and type checks pass.
- Earlier channel screen: user confirmed Up/Stop/Down move and stop the physical motor.
  TX was also observed completing during a panel refresh.
- Expanded UI initial image: full refresh completed in 4.531 s; radio remained healthy.
  A 54 ms drawing warning prompted removal of redundant per-label background painting.
- Final image hardware capture: `/private/tmp/paper-mono-ui-final-serial.log`.
  Full cleanup measured 4.538 s; partial page cycles 911–916 ms during user interaction.
  No component latency warnings were observed in that capture after the drawing fix.
- The user confirmed readable pages and correct navigation. They renamed the remote
  to `WINDOWBANK`; USB logs confirmed the NVS write. After reboot the core restored
  that named remote, and the frontend restored its selected source `4FCA30` with
  sniffing disabled. Capture: `/private/tmp/paper-mono-ui-persistence-serial.log`.
- The user confirmed the remote-detail controls/Clear key fix works.
- Icon UI: native firmware compiled without compiler warnings and uploaded with
  flash hashes verified. USB logs show the running board and hub healthy, with RF
  reception and TX completion. Captures: `/private/tmp/paper-mono-icons-build.log`,
  `/private/tmp/paper-mono-icons-flash.log`, `/private/tmp/paper-mono-icons-serial.log`.
- Group/learn-in workflows and the subsequent icon presentation still need physical
  user validation. Compilation is not evidence those workflows have been exercised
  on hardware.
- No microSD is inserted (user confirmed). SD file operations are implemented and
  compiled, but mount/export/restore are not hardware validated. Mount/file operations
  are synchronous maintenance actions; their main-loop latency needs measuring with
  a card before claiming responsiveness during backup/restore.
- No new tests were added or existing tests modified, per project instructions.

## References

- [Official PaperMono specifications and pin map](https://docs.m5stack.com/en/core/PaperMono)
- [Factory SD HAL](https://github.com/m5stack/M5PaperMono-UserDemo/blob/c1099107271d31a0678d661a896e2b04dbb331ea/main/hal/hal_tf_card.cpp)
- [ESP-IDF 5.5.2 FAT/VFS API](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-reference/storage/fatfs.html)
- [Initial bring-up and TX evidence](paper-mono-bringup.md)
