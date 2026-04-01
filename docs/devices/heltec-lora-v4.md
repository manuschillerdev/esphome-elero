# Heltec WiFi LoRa 32 V4

ESP32-S3 board with an onboard SX1262 LoRa radio -- no external CC1101 module needed.

## ESPHome Config

```yaml
esphome:
  name: "heltec-lora-v4"
  on_boot:
    priority: 800
    then:
      - output.turn_on: vext_enable
  platformio_options:
    board_build.mcu: esp32s3
    board_build.name: "Heltec WiFi LoRa 32 V4"
    board_build.upload.flash_size: "16MB"
    board_build.upload.maximum_size: 16777216
    board_build.vendor: "Heltec"
    board_build.f_flash: 80000000L
    board_build.arduino.memory_type: qio_opi

esp32:
  board: esp32-s3-devkitc-1
  variant: esp32s3
  framework:
    type: esp-idf
    sdkconfig_options:
      CONFIG_VFS_SUPPORT_SELECT: "y"
  flash_size: 16MB

# VEXT_ENABLE (GPIO36) active-low — powers OLED + LoRa antenna boost/PA.
# Must be LOW for TX to work.
output:
  - platform: gpio
    pin:
      number: GPIO36
      inverted: true
    id: vext_enable

spi:
  clk_pin: GPIO9
  mosi_pin: GPIO10
  miso_pin: GPIO11

elero:
  radio: sx1262
  cs_pin: GPIO8
  irq_pin: GPIO14      # DIO1
  busy_pin: GPIO13
  rst_pin: GPIO12
  rf_switch: true       # DIO2 for RF switch control
  tcxo_voltage: 1.8     # TCXO on DIO3 at 1.8V
```

## Notes

- The SX1262 driver is **experimental** -- see [SX1262 driver status](../../CLAUDE.md) for details.
- No external wiring required -- the radio is onboard.
- `vext_enable` must be turned on at boot for TX to work.
