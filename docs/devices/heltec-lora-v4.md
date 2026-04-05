# Heltec WiFi LoRa 32 V4

ESP32-S3 board with an onboard SX1262 LoRa radio -- no external CC1101 module needed.

## Hardware Variants

The Heltec V4 has gone through several hardware revisions with different Front-End Modules (FEM). The FEM provides PA (TX amplification) and LNA (RX amplification) and must be properly configured for the radio to work at full range.

| Version | FEM Chip | PA Pin (CPS/CTX) | Notes |
|---------|----------|------------------|-------|
| V4.2 | GC1109 | GPIO46 | Most common variant |
| V4.3.1+ | KCT8103L | GPIO5 | GPIO46 freed for user apps |

All versions share:
- **GPIO7**: FEM LDO power supply
- **GPIO2**: FEM chip enable (CSD)
- **DIO2**: TX/RX path select (automatic via `rf_switch: true`)

The FEM control logic is the same across variants: power on, enable chip, toggle PA pin for TX/RX. Only the PA mode pin changed from GPIO46 to GPIO5. The three-pin driver interface covers both variants.

## ESPHome Config

### V4.2 (GC1109)

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
  rf_switch: true       # DIO2 controls GC1109 CTX pin (TX/RX path select)
  tcxo_voltage: 1.8     # TCXO on DIO3 at 1.8V
  # GC1109 FEM control — must be powered BEFORE SX1262 calibration
  fem_power_pin: GPIO7  # FEM LDO power supply
  fem_enable_pin: GPIO2 # FEM chip enable (CSD)
  fem_pa_pin: GPIO46    # FEM PA mode (CPS: HIGH=full PA, LOW=bypass/LNA)
```

### V4.3.1+ (KCT8103L)

Same config, only the PA pin changes:

```yaml
elero:
  radio: sx1262
  cs_pin: GPIO8
  irq_pin: GPIO14
  busy_pin: GPIO13
  rst_pin: GPIO12
  rf_switch: true
  tcxo_voltage: 1.8
  fem_power_pin: GPIO7
  fem_enable_pin: GPIO2
  fem_pa_pin: GPIO5     # KCT8103L uses GPIO5 instead of GPIO46
```

## Pin Mapping

| Signal | GPIO | Notes |
|--------|------|-------|
| SPI CLK | 9 | |
| SPI MOSI | 10 | |
| SPI MISO | 11 | |
| SPI CS (NSS) | 8 | |
| DIO1 (IRQ) | 14 | Rising edge for RX_DONE/TX_DONE |
| BUSY | 13 | HIGH when radio is processing |
| RST | 12 | Active LOW reset |
| VEXT_ENABLE | 36 | Active LOW — powers OLED + LoRa PA circuit |
| FEM Power | 7 | FEM LDO power supply |
| FEM Enable | 2 | FEM chip enable (CSD) |
| FEM PA Mode | 46 (V4.2) / 5 (V4.3.1) | PA mode select (CPS/CTX) |
| DIO2 | (internal) | RF switch — automatic via `rf_switch: true` |

## Notes

- The FEM pins (`fem_power_pin`, `fem_enable_pin`) are set HIGH inside the driver's `init()`, **before** the SX1262 reset and calibration sequence. This is critical — the SX1262 must see the correct antenna impedance during image calibration. Using YAML `on_boot` for these pins does not work because it runs after component setup.
- `vext_enable` (GPIO36) must be turned on at boot for the RF path to work.
- No external wiring required — the radio and FEM are onboard.
- Check the version printed on the back of the PCB to determine your variant.
