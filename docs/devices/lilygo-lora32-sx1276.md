# LILYGO LoRa32 V2.1 (SX1276)

ESP32 board with an onboard SX1276 868 MHz LoRa radio -- no external CC1101 module needed. Also sold as LILYGO T3 V1.6.

## Pin Mapping

| Function | GPIO |
|----------|------|
| SPI SCK | GPIO5 |
| SPI MOSI | GPIO27 |
| SPI MISO | GPIO19 |
| SPI CS (NSS) | GPIO18 |
| Radio RST | GPIO23 |
| Radio DIO0 (IRQ) | GPIO26 |
| Radio DIO1 | GPIO33 |
| Radio DIO2 | GPIO32 |
| OLED SDA | GPIO21 |
| OLED SCL | GPIO22 |
| LED | GPIO25 |

## ESPHome Config

```yaml
esphome:
  name: "lilygo-lora32"
  platformio_options:
    build_unflags: -std=gnu++11
    build_flags:
      - -std=gnu++2a

esp32:
  board: esp32dev
  framework:
    type: arduino

spi:
  clk_pin: GPIO5
  mosi_pin: GPIO27
  miso_pin: GPIO19

elero:
  radio: sx1276
  cs_pin: GPIO18
  irq_pin: GPIO26      # DIO0
  rst_pin: GPIO23
  pa_power: 17          # +17 dBm (PA_BOOST), max +20 dBm
```

## Notes

- The SX1276 driver is **experimental** -- hardware-verified TX/RX with CC1101 receivers is pending.
- No external wiring required -- the radio is onboard.
- The SX1276 uses software CRC-16 and PN9 whitening for CC1101 compatibility (hardware implementations differ).
- PA power range: -1 to +20 dBm. Default +17 dBm uses PA_BOOST. Set `pa_power: 20` for maximum output (+20 dBm mode with PA_DAC).
- Also compatible with other SX1276-based boards (T-Beam original, T3-S3 V1.2) -- adjust pin mapping accordingly.
