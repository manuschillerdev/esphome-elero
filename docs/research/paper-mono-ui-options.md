# PaperMono native UI options

Research date: 2026-09-22. This is a design recommendation, not hardware validation. No firmware or tests were changed for this study.

## Recommendation

Start with **ESPHome display drawing + touch + a small `OutputAdapter`**, using the manufacturer's OTP refresh sequence. Prove one screen with device name, state, and OPEN / STOP / CLOSE. Once that works, **ESPHome's LVGL integration is the strongest route to a polished multi-page frontend**. Keep the panel driver underneath either renderer.

Do not begin by importing the complete M5Stack factory application or by inventing a cross-board UI framework. Compare the working spike with `main` before extracting interfaces, as agreed. A modern result here means clear typography, consistent controls, immediate command handling, and efficient event-driven redraws. Animation is not a useful objective for this panel.

## Hardware and panel constraints

The full PaperMono uses ESP32-S3R8, 16 MB flash, 8 MB PSRAM, a 480×800 SSD1677 display, FT6336G touch, frontlight, two user buttons, and a 1150 mAh battery. Display SPI and radio SPI have separate board wiring. Touch uses the system I²C bus and interrupt GPIO4; display and touch reset/power are expander-controlled. The effective touch area stops short of the outer five pixels. [Official specifications and pin map](https://docs.m5stack.com/en/core/PaperMono)

M5Stack currently calls its PaperMono M5GFX waveforms unstable and recommends the manufacturer's OTP example. Its guidance is a full refresh after approximately ten partial fast refreshes. Use that as the initial policy, then validate visible ghosting on the actual hardware. Published M5GFX timing figures are not measurements of our eventual OTP driver. [Official driver notes](https://docs.m5stack.com/en/core/PaperMono)

The OTP demo establishes a monochrome RAM baseline, uses an inverted synchronization phase followed by OTP mode 1 for full monochrome refresh, and uses `0xFF` for partial refresh. Its partial operation sends a complete framebuffer: **partial waveform refresh does not mean a dirty rectangle SPI transfer**. Deep sleep mode 1 retains controller RAM; grayscale-to-monochrome transitions rebuild the baseline. [Pinned OTP implementation](https://github.com/m5stack/M5PaperMono-OTP-Demo/blob/c7c02554f89fd06f80d988b805b2a59050c78a46/components/EDP_OTP_LUT_demo/src/EDP_OTP_LUT_demo.cpp)

## Options

| Route | Benefits | Cost or limitation | Assessment |
| --- | --- | --- | --- |
| ESPHome drawing + touch | Existing component lifecycle, fonts, drawing primitives, few dependencies; simple to connect registry callbacks to a dirty flag | Layout, hit regions, navigation and dynamic lists are our responsibility | Smallest first working UI |
| ESPHome LVGL | Widgets, layout, focus, touch events, styles, dynamic controls; established ESPHome integration | More memory and integration work; still needs a correct panel driver; version matters | Preferred richer UI after panel bring-up |
| M5GFX + M5Unified | Vendor board integration and drawing API, factory examples for touch/power/display | Waveform caveat; blocking boundaries; overlapping ownership of buses/power with ESPHome | Hardware reference and fallback, not the default full application stack |
| Slint | Declarative UI and embedded software rendering | Additional runtime/toolchain/platform port, no evaluated ready-made PaperMono/ESPHome integration | Attractive in a separate embedded product; little benefit for this spike |

ESPHome provides both its own drawing engine and LVGL over display components. Touch can be wired into ESPHome controls or LVGL. [Display architecture](https://esphome.io/components/display/), [LVGL integration](https://esphome.io/components/lvgl/), [FT63X6 component](https://esphome.io/components/touchscreen/ft63x6/)

FT63X6 is a candidate for the FT6336G, not a tested compatibility claim. Verify controller identification, touch reset/power sequencing, axis orientation and edge coordinates on this board.

Slint provides a software-rendered embedded route, but using it here would add platform integration before delivering a useful blind controller. It is not intrinsically more efficient because its UI language is newer. [Slint embedded overview](https://slint.dev/embedded)

## Driver choice is separate from renderer choice

ESPHome's `epaper_spi` architecture already uses a queued state machine and avoids waiting synchronously for BUSY. This is a good fit for our time-sensitive main loop. A display component implementing that contract can support either basic drawing or LVGL. [ESPHome ePaper SPI architecture](https://esphome.io/components/display/epaper_spi/)

However, the inspected ESPHome 2026.9.0 `SSD1677` model selects `EPaperMono`. Its generic full-refresh command is `0xF7`, it initially clears the second RAM plane, and its partial-update sleep behavior differs from the vendor OTP example. Matching the controller name alone does not validate the panel sequence. Prefer a small PaperMono-specific panel implementation using the vendor sequence inside the existing nonblocking architecture, if the generic model cannot be demonstrated equivalent. [SSD1677 model](https://github.com/esphome/esphome/blob/2026.9.0/esphome/components/epaper_spi/models/ssd1677.py), [generic monochrome implementation](https://github.com/esphome/esphome/blob/2026.9.0/esphome/components/epaper_spi/epaper_spi_mono.cpp)

The OTP SPI helper polls BUSY using `vTaskDelay()`. This yields CPU time but still blocks its calling task. Copying it into the ESPHome main loop would delay registry work. Convert those waits to state transitions/timeouts, or isolate the unmodified blocking implementation in a dedicated display task for the spike. [OTP SPI helper](https://github.com/m5stack/M5PaperMono-OTP-Demo/blob/c7c02554f89fd06f80d988b805b2a59050c78a46/components/EDP_OTP_LUT_demo/src/EDP_SPI.cpp)

If a display task is used, it receives an immutable frame and never accesses the registry. Coordinate expander/I²C access with radio reset/recovery; separate display SPI wiring does not eliminate shared I²C access. Keep ownership explicit rather than initializing the same buses through ESPHome and M5Unified independently.

The factory firmware demonstrates refresh requests, refresh counters and delayed panel power saving, but contains `waitDisplay()` calls at several boundaries. Those calls require the same review before reuse. The factory is an ESP-IDF application with pinned components and adaptations; importing its entire dependency set is not necessary to draw three buttons. [Factory display code](https://github.com/m5stack/M5PaperMono-UserDemo/blob/c1099107271d31a0678d661a896e2b04dbb331ea/main/hal/hal_display.cpp), [factory build description](https://github.com/m5stack/M5PaperMono-UserDemo/blob/c1099107271d31a0678d661a896e2b04dbb331ea/README.md)

## Version and framework discipline

At research time the worktree's `pyproject.toml` pins **ESPHome 2026.2.4**, whose LVGL integration selects **8.4.0**. The original repository's existing `.venv` instead contains **ESPHome 2026.9.0**, whose integration selects **LVGL 9.5.0**. Do not use the current website or shared virtual environment as evidence of the pinned version's API. [Pinned LVGL configuration](https://github.com/esphome/esphome/blob/2026.2.4/esphome/components/lvgl/__init__.py), [2026.9.0 LVGL configuration](https://github.com/esphome/esphome/blob/2026.9.0/esphome/components/lvgl/__init__.py)

In the inspected 2026.9.0 LVGL implementation, `update_when_display_idle` delays the rendering timer while the panel is busy, but continues input and other timers. That behavior is particularly useful for STOP handling. Verify the equivalent behavior in whichever version the spike actually builds; do not infer it from the option name. [LVGL loop implementation](https://github.com/esphome/esphome/blob/2026.9.0/esphome/components/lvgl/lvgl_esphome.cpp)

Use ESPHome GPIO/I²C/SPI interfaces in the eventual component to preserve the project's ESP-IDF and Arduino support. The initial hardware validation can target ESP-IDF without introducing a second frontend implementation. The OTP demonstration itself is an ESP-IDF reference, not proof of Arduino compatibility for a copied component. [OTP project](https://github.com/m5stack/M5PaperMono-OTP-Demo/blob/c7c02554f89fd06f80d988b805b2a59050c78a46/README.md)

## Connection to the existing core

The current [`OutputAdapter`](../../components/elero/output_adapter.h) already supplies device, configuration, group and state notifications. The Paper frontend needs only presentation state: selected stable device/group identifier, current page and dirty fields. It reads domain state from the registry on the main loop and sends touch commands through registry APIs. Do not retain a `Device&` across removal or queue registry pointers to another task.

```text
Registry callback → mark visible fields dirty → compose latest frame → panel refresh
Touch/button      → resolve selected target   → registry command
```

While refreshing, coalesce subsequent display updates into the latest desired frame. Commands remain independent of refresh completion. Clear/reselect the visible target if a device is removed. Do not derive a new motor state machine inside the screen or update it on every raw RF packet.

Start with stable controls: device name, textual state, three large buttons, previous/next device. Use presets rather than a continuously dragged position slider. Give immediate optional buzzer feedback when a command is accepted; the slower screen can show the confirmed state later. A physical STOP shortcut should remain usable during full-screen refresh. Avoid a per-second clock or animated progress indicators that force unnecessary panel activity.

## Memory, power and iteration

Calculated from 480×800 pixels: one packed monochrome frame is **48,000 bytes**, two frames are **96,000 bytes**, four grayscale levels need **96,000 bytes** per packed frame, and a full RGB565 frame is **768,000 bytes**. These are pixel-storage calculations, not measured whole-firmware usage. Font data, widget objects, staging buffers, Wi-Fi and RF queues add overhead.

PSRAM makes several approaches plausible, but a one-bit UI with bounded frame storage keeps allocation simple. LVGL may use a color staging buffer even for a monochrome panel; measure actual allocations instead of assuming native packed monochrome throughout the ESPHome integration. Partial rendering in LVGL reduces CPU drawing work; it does not automatically choose a safe panel waveform.

Switch the panel into its validated idle mode after refresh and timeout the frontlight. Keep the hub awake initially: screen sleep and ESP32 deep sleep are different decisions. Sleeping the MCU changes radio reception and state tracking, so battery life cannot be estimated from the panel's idle consumption alone.

ESPHome SDL can accelerate standalone layout iteration on a desktop. Use synthetic presentation data only: this project's core does not support the host platform, and the simulator cannot validate e-paper waveform, ghosting or touch hardware. [ESPHome SDL display](https://esphome.io/components/display/sdl/)

## Suggested sequence and evidence required

1. Show a static monochrome frame with vendor OTP full refresh; confirm portrait coordinates.
2. Read touch and physical buttons with panel refresh idle and busy.
3. Attach the minimal adapter and operate one existing device.
4. Measure main-loop responsiveness, command delivery and RF reception during full/partial updates, including calibrated intermediate STOP timing.
5. Validate repeated partial updates, periodic cleanup, reboot/baseline recovery and display timeout recovery.
6. Compare the spike with `main`. Choose basic drawing if the UI remains tiny; adopt ESPHome LVGL for lists, groups, reusable layouts and additional pages once its value is concrete.

Open hardware questions: actual OTP refresh duration, ghosting cadence, FT6336G compatibility, buffer/heap usage, I²C coordination during radio recovery, and display/touch behavior across panel power transitions. These have not been measured by this research task.
