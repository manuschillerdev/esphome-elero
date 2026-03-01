# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## 1.0.0 (2026-03-01)


### Features

* add devcontainer for Claude Code development ([850d0d0](https://github.com/manuschillerdev/esphome-elero/commit/850d0d0dd3319af57d32f1146d6309ef8dcb76f1))
* add devcontainer for Claude Code development ([fbca94a](https://github.com/manuschillerdev/esphome-elero/commit/fbca94a8fc17aacad4d37b47232920e3a2461abd))
* add light control platform for Elero light actuators ([e619695](https://github.com/manuschillerdev/esphome-elero/commit/e619695eab0c7e8b02de531d6ad502fe18591505))
* add light control platform for Elero light actuators ([d1bdaa9](https://github.com/manuschillerdev/esphome-elero/commit/d1bdaa9e3d5cc9df711eed5177431d6483db3a36))
* implement non-blocking TX with callback-based ownership ([a08d0ff](https://github.com/manuschillerdev/esphome-elero/commit/a08d0ff34536ecffdfebf7612177dc46a0062623))
* implement non-blocking TX with callback-based ownership ([0a9e42c](https://github.com/manuschillerdev/esphome-elero/commit/0a9e42c4b446c7a67b05bc617dcdc5e9ab4710da))
* migrate to websockets (mongoose) ([#6](https://github.com/manuschillerdev/esphome-elero/issues/6)) ([800f1ab](https://github.com/manuschillerdev/esphome-elero/commit/800f1ab7b016a7483a859aac7291d7bb92af3d21))
* serve only /elero UI; redirect / to /elero ([6758015](https://github.com/manuschillerdev/esphome-elero/commit/675801568b3655e7cafd0abb8dbffa809c561dc4))
* serve only /elero UI; redirect / to /elero ([b6f8560](https://github.com/manuschillerdev/esphome-elero/commit/b6f8560f896db4d430efcd84543d8f5a8dae6282))
* **web:** migrate to WebSocket-only communication ([60e395b](https://github.com/manuschillerdev/esphome-elero/commit/60e395bed7c5998c9d263c9b02144645f3f7f6eb))


### Bug Fixes

* add const to canHandle() to match ESPHome web_server_idf signature ([5c51fdd](https://github.com/manuschillerdev/esphome-elero/commit/5c51fdd8942ab29b5e5dd1981485ab601c7b9d56))
* add const to canHandle() to match ESPHome web_server_idf signature ([42ec799](https://github.com/manuschillerdev/esphome-elero/commit/42ec799172991d06241a0dd3a9f7f0a60c228768))
* add SPI error handling for CC1101 communication ([8c9bfc3](https://github.com/manuschillerdev/esphome-elero/commit/8c9bfc324122e918b9a2e425ea2b1fda0f184dd6))
* apply modern C++ and ESP32 best practices to phase1 changes ([57a00c6](https://github.com/manuschillerdev/esphome-elero/commit/57a00c6bb49941fd8fd3b42d7c753dff8bb0f08c))
* bypass CC1101 CCA in transmit() to prevent TX timeout ([740a208](https://github.com/manuschillerdev/esphome-elero/commit/740a208d47daa03cbb0cdd0c41151767995d25d3))
* bypass CC1101 CCA in transmit() to prevent TX timeout ([47eae6e](https://github.com/manuschillerdev/esphome-elero/commit/47eae6ef0bc671233094235c48fcdd3186bc32aa))
* **ci:** add PlatformIO caching and fix elero_web dependencies ([1054204](https://github.com/manuschillerdev/esphome-elero/commit/1054204b059fe344da1b682e02d768416d78f686))
* **ci:** cancel in-progress runs on new push ([6118407](https://github.com/manuschillerdev/esphome-elero/commit/611840741774c186417d9a4ff9b2b149b5bd6bdc))
* **ci:** exclude elero_web from ESP32 tests ([7d1fce9](https://github.com/manuschillerdev/esphome-elero/commit/7d1fce9c314a48cb48e817a816fc1fb1b6f05650))
* **ci:** handle [[nodiscard]] warnings and fix IDF tests ([93f7431](https://github.com/manuschillerdev/esphome-elero/commit/93f7431a54b16319dcad116f060cb50170a6fd42))
* **ci:** only trigger push on main branch ([ff0c783](https://github.com/manuschillerdev/esphome-elero/commit/ff0c7839db3e7e73174d8f1bb7c59f6ed199c5fe))
* **ci:** pin ESPHome 2026.2.2, add uv project setup ([9508e81](https://github.com/manuschillerdev/esphome-elero/commit/9508e810fe889649f94bcdcace795d423cfe4410))
* **ci:** remove elero_web from RP2040 test ([5772331](https://github.com/manuschillerdev/esphome-elero/commit/577233166b89aa6d928f52bb0ae53ef2847a45e1))
* **ci:** use ESPHome Docker container instead of PlatformIO caching ([a341b11](https://github.com/manuschillerdev/esphome-elero/commit/a341b11d6939afb4e07b6bfefdd610b3a932a0e5))
* correct payload index for command byte in cover and light handlers ([4ac8e3d](https://github.com/manuschillerdev/esphome-elero/commit/4ac8e3d22ab225908620354eebbcf4ce0956d2bf))
* correct payload index for command byte in cover and light handlers ([53415b1](https://github.com/manuschillerdev/esphome-elero/commit/53415b158ef5357277232e6fc4fa4546d91dba7d))
* discovery now extracts params from 6a command packets, not CA responses ([09aface](https://github.com/manuschillerdev/esphome-elero/commit/09afaced200eff3557e0774ce12a23c2e58030fc))
* discovery now extracts params from 6a command packets, not CA responses ([1f72f16](https://github.com/manuschillerdev/esphome-elero/commit/1f72f1697fefc5900fc683ae945e74bf1be1c31f))
* **elero_web:** add AsyncTCP library dependency ([70b1cd9](https://github.com/manuschillerdev/esphome-elero/commit/70b1cd9e80fabbbc15574a738eb28d7c61206a6a))
* **elero_web:** add explicit ESPAsyncWebServer library dependency ([0a2c69f](https://github.com/manuschillerdev/esphome-elero/commit/0a2c69f8c92fb67a60637eb1da639bd1f7646bdf))
* **elero_web:** add WiFi, FS, Update library dependencies ([fea6615](https://github.com/manuschillerdev/esphome-elero/commit/fea6615c9eba79bf24d2efea0f23b0c79afbad09))
* **elero_web:** don't pin ESPAsyncWebServer version ([8f9cc0f](https://github.com/manuschillerdev/esphome-elero/commit/8f9cc0fc993374ac8f45de708188df844aaf290e))
* escape user-controlled strings in JSON responses (issue [#26](https://github.com/manuschillerdev/esphome-elero/issues/26)) ([9a2d247](https://github.com/manuschillerdev/esphome-elero/commit/9a2d24736332ee2b0043faf4724ef26fd89f0007))
* escape user-controlled strings in JSON responses to prevent injection (issue [#26](https://github.com/manuschillerdev/esphome-elero/issues/26)) ([b2226d7](https://github.com/manuschillerdev/esphome-elero/commit/b2226d7f47a328a1bebf3da969b715e04cddf5f7))
* **light:** prevent feedback loop in set_rx_state ([f9b5b9b](https://github.com/manuschillerdev/esphome-elero/commit/f9b5b9bffa63b792a9b850a4048be11b6e1b8913))
* replace atomic methods with volatile access in elero.cpp ([b42e377](https://github.com/manuschillerdev/esphome-elero/commit/b42e377f97bb9060f3e6febe5698a88573968f28))
* resolve CI failures (formatting, linting, test configs) ([67f79dd](https://github.com/manuschillerdev/esphome-elero/commit/67f79dd718e6c21844dee53670e884c93df8e721))
* resolve CI failures and remove format enforcement ([39a1f57](https://github.com/manuschillerdev/esphome-elero/commit/39a1f57c1763ce4b3b9a3b915bba140641c2199e))
* revert payload[8] regression and fix issue [#22](https://github.com/manuschillerdev/esphome-elero/issues/22) — use payload[4] for command byte in all paths ([6ad7569](https://github.com/manuschillerdev/esphome-elero/commit/6ad756938e68358d82238ed62fc40a2492722ab7))
* revert payload[8] regression and fix issue [#22](https://github.com/manuschillerdev/esphome-elero/issues/22) — use payload[4] for command byte in all paths ([6128def](https://github.com/manuschillerdev/esphome-elero/commit/6128def6149ba59dc4690964299b25b2b8fb7446))
* trigger immediate poll when physical remote command is detected ([029e3d2](https://github.com/manuschillerdev/esphome-elero/commit/029e3d2fe803be1810114e8a69d683d7dcbff45f)), closes [#34](https://github.com/manuschillerdev/esphome-elero/issues/34)
* update elero_web for ESPHome 2025.6 API compatibility ([d14a690](https://github.com/manuschillerdev/esphome-elero/commit/d14a69021fb4b59bf67e819a96b4ddf57d641267))
* use volatile instead of std::atomic for ESP8266/RP2040 compat ([c926c98](https://github.com/manuschillerdev/esphome-elero/commit/c926c9849b27ba63c205ce9460d383c27a9c2e14))


### Refactoring

* add SpiTransaction RAII guard and C++17 modernization ([44636fc](https://github.com/manuschillerdev/esphome-elero/commit/44636fcba192d70b9aab16dd05797c33c17acf63))
* apply modern C++ conventions to codebase ([4b0dee4](https://github.com/manuschillerdev/esphome-elero/commit/4b0dee4140814b22bebd99296f914f6d587fd8f1))
* **ci:** focus CI targets on ESP32-S3 and ESP32 ([b756b25](https://github.com/manuschillerdev/esphome-elero/commit/b756b256b78649d507db1be33aa188a5a3ecfc45))
* **ci:** focus CI targets on ESP32-S3 and ESP32 ([d81c6e4](https://github.com/manuschillerdev/esphome-elero/commit/d81c6e40d08e3d89310fcbd2a0704d312cbe082d))
* core methods enforce their own invariants ([86aa7ca](https://github.com/manuschillerdev/esphome-elero/commit/86aa7ca70771fa5df1777af554d6f60266af92c8))
* **devcontainer:** slim down base image and fix permissions ([176f99e](https://github.com/manuschillerdev/esphome-elero/commit/176f99e2f49315acd396872ad1ecfb0fd18d8c1e))
* extract shared command handling to CommandSender class ([e35d348](https://github.com/manuschillerdev/esphome-elero/commit/e35d348abaa71d9812ebd73f9f6809d3b775e681))
* modernize cc1101.h and fix cover position tracking ([c47260b](https://github.com/manuschillerdev/esphome-elero/commit/c47260bf4e07729010ebe735a1e521df3be0b9e7))
* move business logic from web layer to core ([c2f6bb1](https://github.com/manuschillerdev/esphome-elero/commit/c2f6bb1d447f0bf34eeb564a41105a9fbda025b5))
* replace dim booleans with DimDirection enum ([54cf536](https://github.com/manuschillerdev/esphome-elero/commit/54cf5366301470802e99a057a5b15c093c425726))
* web UI uses same code path as Home Assistant ([678713c](https://github.com/manuschillerdev/esphome-elero/commit/678713c8bae553db7133eba2396f6d43df9f905b))


### Documentation

* add CHANGELOG.md ([4761984](https://github.com/manuschillerdev/esphome-elero/commit/4761984704cbdb1cbbb855e34fad93c987d6ebe2))
* add Claude skills for protocol and development guidelines ([8eceeb9](https://github.com/manuschillerdev/esphome-elero/commit/8eceeb9217669276875b0b783899d952a307c2d2))
* add elero-protocol skill ([f75f4a1](https://github.com/manuschillerdev/esphome-elero/commit/f75f4a180c1db0b78683a6d5c2688319e3af9b88))
* add elero-protocol skill reference to CLAUDE.md ([922740a](https://github.com/manuschillerdev/esphome-elero/commit/922740a704ee1395c77a461dd0806a7adee20bb9))
* add ESP platform constraints to modern-cpp skill ([90b570b](https://github.com/manuschillerdev/esphome-elero/commit/90b570b8f33e4f391f9084e7e3baa7bf663433c9))
* add esp32-development skill ([9af7a23](https://github.com/manuschillerdev/esphome-elero/commit/9af7a23b3389762876ee23f30301b38115de7308))
* add light example to example.yaml ([7012b80](https://github.com/manuschillerdev/esphome-elero/commit/7012b802371a22ac00d5e15f0194bc1d8ddf7425))
* add light platform documentation ([cd44cb8](https://github.com/manuschillerdev/esphome-elero/commit/cd44cb8fa603ba5f75aaf52c0da3b2139abfd7e8))
* add migration note for atomic to volatile ([91eac4a](https://github.com/manuschillerdev/esphome-elero/commit/91eac4abc9410224adb65184cbee9b9ce8a038c4))
* add modern-cpp and esp32-development skills ([83eeaf0](https://github.com/manuschillerdev/esphome-elero/commit/83eeaf0bac405018f1c37c3a38c477c4e6b66839))
* add security model documentation ([10d8deb](https://github.com/manuschillerdev/esphome-elero/commit/10d8debe5b5cd6f519bd59843dc61ea08e23a527))
* add tooling section and ESP32 conventions to modern-cpp skill ([e454bbc](https://github.com/manuschillerdev/esphome-elero/commit/e454bbcb5e1cda3b61c4ed8fcb20d1038ce29c43))
* close documentation gaps identified by heartbeat audit ([866df71](https://github.com/manuschillerdev/esphome-elero/commit/866df71d4e4f658687430c51dbdee0ed0be76631))
* update modern-cpp skill for ESP32/embedded (Google Style) ([f726d4a](https://github.com/manuschillerdev/esphome-elero/commit/f726d4ae017bf904a1bc6bdce298b1022006e8c5))

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
- Protocol implementation based on [QuadCorei8085/elero_protocol](https://github.com/QuadCorei8085/elero_protocol)
- Additional protocol research from [stanleypa/eleropy](https://github.com/stanleypa/eleropy)
- Originally forked from [andyboeh/esphome-elero](https://github.com/andyboeh/esphome-elero)

[Unreleased]: https://github.com/pfriedrich84/esphome-elero/compare/main...HEAD
[0.2.0]: https://github.com/pfriedrich84/esphome-elero/releases/tag/v0.2.0
[0.1.0]: https://github.com/pfriedrich84/esphome-elero/releases/tag/v0.1.0
