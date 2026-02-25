# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- `.clang-format` configuration for consistent C++ code style
- This CHANGELOG file

### Changed
- Documentation improvements for clarity

## [0.2.0] - 2025-02

### Added
- **Light platform**: Control Elero light actuators with on/off and brightness support
- **Web UI redesign**: Tabbed interface with discovery, control, diagnostics, and log viewer
- **Runtime blind adoption**: Adopt discovered blinds without recompiling firmware
- **CC1101 runtime frequency tuning**: Change RF frequency via web API
- **Auto-sensors**: Automatically generate RSSI and status text sensors per cover
- **Packet dump mode**: Capture and analyze raw RF packets via web API
- **Log capture**: View recent ESPHome logs through the web UI

### Fixed
- Discovery now extracts parameters from 6a command packets (higher quality than CA responses)
- TX timeout caused by CC1101 CCA (Clear Channel Assessment) blocking transmit
- JSON injection vulnerability in web API responses (issue #26)
- Correct payload index for command byte in cover and light handlers
- Map iteration in `interpret_msg()` accessing pair members correctly
- Duration validation type comparison error

### Changed
- Runtime blinds storage changed from vector to map for O(1) lookups
- Web server now serves only `/elero` UI; root `/` redirects to `/elero`
- Upstream repository link updated to pfriedrich84 fork

## [0.1.0] - 2024

### Added
- Initial release as ESPHome external component
- Cover platform for Elero blind control (open, close, stop, tilt)
- Position tracking via timing-based dead reckoning
- RF discovery scan to find nearby blinds
- RSSI sensor per blind address
- Status text sensor per blind address
- Scan button platform to trigger RF discovery
- Web UI for discovery and YAML generation
- Support for 868.35 MHz and 868.95 MHz frequencies

### Credits
- Encryption/decryption based on [QuadCorei8085/elero_protocol](https://github.com/QuadCorei8085/elero_protocol) (MIT)
- Remote handling based on [stanleypa/eleropy](https://github.com/stanleypa/eleropy) (GPLv3)
- Originally forked from [andyboeh/esphome-elero](https://github.com/andyboeh/esphome-elero)

[Unreleased]: https://github.com/pfriedrich84/esphome-elero/compare/main...HEAD
[0.2.0]: https://github.com/pfriedrich84/esphome-elero/releases/tag/v0.2.0
[0.1.0]: https://github.com/pfriedrich84/esphome-elero/releases/tag/v0.1.0
