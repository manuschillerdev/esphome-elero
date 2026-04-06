# LilyGO T-Embed

ESP32-S3 board with an onboard CC1101 RF module and RF band-select switches.

## ESPHome Config

```yaml
esphome:
  name: "lilygo-t-embed"
  platformio_options:
    board_build.mcu: esp32s3
    board_build.name: "LilyGO T-Embed ESP32-S3"
    board_build.upload.flash_size: "16MB"
    board_build.upload.maximum_size: 16777216
    board_build.vendor: "LilyGO"
    board_build.f_flash: 80000000L
    board_build.arduino.memory_type: qio_opi
  on_boot:
    priority: 800
    then:
      - switch.turn_on: power_on
      - switch.turn_on: cc1101_power_on
      # SW1:0 SW0:1 = 868/915MHz
      - switch.turn_off: rf_sw1
      - switch.turn_on: rf_sw0

esp32:
  board: esp32-s3-devkitc-1
  variant: esp32s3
  framework:
    type: esp-idf
    sdkconfig_options:
      CONFIG_VFS_SUPPORT_SELECT: "y"
  flash_size: 16MB

switch:
  - platform: gpio
    pin: { number: GPIO15, mode: { output: true } }
    name: "CC1101 Power"
    id: cc1101_power_on
    restore_mode: ALWAYS_ON
  - platform: gpio
    pin: { number: GPIO46, mode: { output: true } }
    name: "Board Power"
    id: power_on
    restore_mode: ALWAYS_ON
  - platform: gpio
    pin: GPIO47
    id: rf_sw1
    restore_mode: ALWAYS_OFF
  - platform: gpio
    pin: GPIO48
    id: rf_sw0
    restore_mode: ALWAYS_ON

spi:
  clk_pin: GPIO11
  mosi_pin: GPIO9
  miso_pin: GPIO10

elero:
  cs_pin: GPIO12
  gdo0_pin: GPIO3
```

## Notes

- No external wiring required -- the CC1101 is onboard.
- RF band switches must be set at boot (SW1:0, SW0:1 for 868 MHz).
- Board and CC1101 power GPIOs must be turned on explicitly.
