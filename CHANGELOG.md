# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.10.0](https://github.com/manuschillerdev/esphome-elero/compare/v0.9.0...v0.10.0) (2026-03-22)


### ⚠ BREAKING CHANGES

* Configuration parameter names changed:
    - blind_address → dst_address
    - remote_address → src_address
    - pck_inf1 → type
    - pck_inf2 → type2

### Features

* add devcontainer for Claude Code development ([850d0d0](https://github.com/manuschillerdev/esphome-elero/commit/850d0d0dd3319af57d32f1146d6309ef8dcb76f1))
* add devcontainer for Claude Code development ([fbca94a](https://github.com/manuschillerdev/esphome-elero/commit/fbca94a8fc17aacad4d37b47232920e3a2461abd))
* add light control platform for Elero light actuators ([e619695](https://github.com/manuschillerdev/esphome-elero/commit/e619695eab0c7e8b02de531d6ad502fe18591505))
* add light control platform for Elero light actuators ([d1bdaa9](https://github.com/manuschillerdev/esphome-elero/commit/d1bdaa9e3d5cc9df711eed5177431d6483db3a36))
* dedicated FreeRTOS RF task on Core 0 for CC1101 SPI ([a3f86ba](https://github.com/manuschillerdev/esphome-elero/commit/a3f86ba43fdfa9c1bb6022394543718db5c501ac))
* **frontend:** add inline editing, improve blind/light cards and mock server ([00247e5](https://github.com/manuschillerdev/esphome-elero/commit/00247e598e4dd8f674667096b545f6cda930817b))
* **frontend:** add log forwarding ([1ed5d61](https://github.com/manuschillerdev/esphome-elero/commit/1ed5d61bedd553f64549b3bb65140e8cedaa3df2))
* **frontend:** major UI overhaul with Preact + Zustand ([b312cf1](https://github.com/manuschillerdev/esphome-elero/commit/b312cf1d9326afc4183a82e81bdd2a017d5c44c4))
* **frontend:** major UI overhaul with Preact + Zustand ([abdb6b3](https://github.com/manuschillerdev/esphome-elero/commit/abdb6b3ac52808800cff75e7edd2226bc87100e4))
* **frontend:** major UX overhaul — unified tables, signals, derived state ([03abf5c](https://github.com/manuschillerdev/esphome-elero/commit/03abf5cc6590492de7588aa89673e17f3c6981cd))
* **frontend:** UI polish — filters, icons, consistency, empty states ([0839065](https://github.com/manuschillerdev/esphome-elero/commit/0839065bb3ddd1806fe47aa83d8ea2b9a6e83874))
* implement non-blocking TX with callback-based ownership ([a08d0ff](https://github.com/manuschillerdev/esphome-elero/commit/a08d0ff34536ecffdfebf7612177dc46a0062623))
* implement non-blocking TX with callback-based ownership ([0a9e42c](https://github.com/manuschillerdev/esphome-elero/commit/0a9e42c4b446c7a67b05bc617dcdc5e9ab4710da))
* **light:** add state getters and perform_action to EleroLightBase ([ba60b02](https://github.com/manuschillerdev/esphome-elero/commit/ba60b02ffda9d4072fe5e0e5c4470a6011ddea06))
* **light:** add state getters and perform_action to EleroLightBase ([e4e544c](https://github.com/manuschillerdev/esphome-elero/commit/e4e544c1ee3081314cc2f7ee8143dabf04b3de31))
* **logging:** implement JSON logging and update skill documentation ([fe33d18](https://github.com/manuschillerdev/esphome-elero/commit/fe33d186f70dd97a3fc8dc8a42bfb94dc3c7a5e5))
* migrate to websockets (mongoose) ([#6](https://github.com/manuschillerdev/esphome-elero/issues/6)) ([800f1ab](https://github.com/manuschillerdev/esphome-elero/commit/800f1ab7b016a7483a859aac7291d7bb92af3d21))
* modernize C++ codebase from C++17 to C++20 ([7b2498c](https://github.com/manuschillerdev/esphome-elero/commit/7b2498c2cddde10148d46aeaa3648d28f541da76))
* **mqtt:** add MQTT handler modules, stale discovery cleanup, and gateway sensor ([637e336](https://github.com/manuschillerdev/esphome-elero/commit/637e336b6ac7685254ee2822459b2ed6360a193c))
* **mqtt:** add MQTT mode with NVS device persistence ([66ac2d4](https://github.com/manuschillerdev/esphome-elero/commit/66ac2d4f67cb67bd0cb9479f6ab5cbd123ba6fd7))
* **mqtt:** add MQTT mode with NVS persistence and remote control entity ([77ee8bd](https://github.com/manuschillerdev/esphome-elero/commit/77ee8bd026693ee2778b7da5b945be164eeffcb5))
* publish component version and commit hash on WS connect ([c2a9ae8](https://github.com/manuschillerdev/esphome-elero/commit/c2a9ae81257030f39f09abc7579070158ce7a263))
* **radio:** add periodic radio health watchdog ([d31df5b](https://github.com/manuschillerdev/esphome-elero/commit/d31df5b0128f4e3faa687ba11ab9a47f24cb4f18))
* **radio:** harden CC1101 state machine for robustness ([0dbd4b0](https://github.com/manuschillerdev/esphome-elero/commit/0dbd4b01b8cc336cd66a92c8abd0a049ffa407b4))
* restructure config event, radio-aware signal indicator, UI cleanup ([40870cd](https://github.com/manuschillerdev/esphome-elero/commit/40870cdfd7bcc95bb47bc0ccdbad04169aeada49))
* rewrite architecture ([565c726](https://github.com/manuschillerdev/esphome-elero/commit/565c726924e166b81a55f79f4f20e6941ca517db))
* RF observability — internal sensors for Prometheus ([f850830](https://github.com/manuschillerdev/esphome-elero/commit/f850830d051e7eadacf18f15c8a1ef3b678cb725))
* serve only /elero UI; redirect / to /elero ([6758015](https://github.com/manuschillerdev/esphome-elero/commit/675801568b3655e7cafd0abb8dbffa809c561dc4))
* serve only /elero UI; redirect / to /elero ([b6f8560](https://github.com/manuschillerdev/esphome-elero/commit/b6f8560f896db4d430efcd84543d8f5a8dae6282))
* unified state snapshot layer for consistent HA reporting ([09cf600](https://github.com/manuschillerdev/esphome-elero/commit/09cf6006883224c6d17fee5728e894e72dd08b9b))
* **web:** migrate to WebSocket-only communication ([60e395b](https://github.com/manuschillerdev/esphome-elero/commit/60e395bed7c5998c9d263c9b02144645f3f7f6eb))
* WebSocket optimistic updates via OutputAdapter + snapshot to_json ([904b4c5](https://github.com/manuschillerdev/esphome-elero/commit/904b4c5e039c1f9a69017975b5fa2da823f65dbf))


### Bug Fixes

* add const to canHandle() to match ESPHome web_server_idf signature ([5c51fdd](https://github.com/manuschillerdev/esphome-elero/commit/5c51fdd8942ab29b5e5dd1981485ab601c7b9d56))
* add const to canHandle() to match ESPHome web_server_idf signature ([42ec799](https://github.com/manuschillerdev/esphome-elero/commit/42ec799172991d06241a0dd3a9f7f0a60c228768))
* add ring buffer for bigger fifo capacity (solving dropped rx packets) ([7cf49c7](https://github.com/manuschillerdev/esphome-elero/commit/7cf49c7661ac3a03536c2c754c1e8c3cbe438a59))
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
* **cover:** implement listen-first-poll-later strategy for RF polling ([d2145c0](https://github.com/manuschillerdev/esphome-elero/commit/d2145c0605a46fd16a223aa5318c434c6ce0c1cf))
* cross-platform millis ([ee6447c](https://github.com/manuschillerdev/esphome-elero/commit/ee6447cb0e2adeb93e18c3b95d03a854698acb58))
* discovery now extracts params from 6a command packets, not CA responses ([09aface](https://github.com/manuschillerdev/esphome-elero/commit/09afaced200eff3557e0774ce12a23c2e58030fc))
* discovery now extracts params from 6a command packets, not CA responses ([1f72f16](https://github.com/manuschillerdev/esphome-elero/commit/1f72f1697fefc5900fc683ae945e74bf1be1c31f))
* dump fixes for NVS, mqtt and state machine ([e41fac6](https://github.com/manuschillerdev/esphome-elero/commit/e41fac6d1e368ad0cd2311693f261b326b1ce08b))
* **elero_web:** add AsyncTCP library dependency ([70b1cd9](https://github.com/manuschillerdev/esphome-elero/commit/70b1cd9e80fabbbc15574a738eb28d7c61206a6a))
* **elero_web:** add explicit ESPAsyncWebServer library dependency ([0a2c69f](https://github.com/manuschillerdev/esphome-elero/commit/0a2c69f8c92fb67a60637eb1da639bd1f7646bdf))
* **elero_web:** add WiFi, FS, Update library dependencies ([fea6615](https://github.com/manuschillerdev/esphome-elero/commit/fea6615c9eba79bf24d2efea0f23b0c79afbad09))
* **elero_web:** don't pin ESPAsyncWebServer version ([8f9cc0f](https://github.com/manuschillerdev/esphome-elero/commit/8f9cc0fc993374ac8f45de708188df844aaf290e))
* escape user-controlled strings in JSON responses (issue [#26](https://github.com/manuschillerdev/esphome-elero/issues/26)) ([9a2d247](https://github.com/manuschillerdev/esphome-elero/commit/9a2d24736332ee2b0043faf4724ef26fd89f0007))
* escape user-controlled strings in JSON responses to prevent injection (issue [#26](https://github.com/manuschillerdev/esphome-elero/issues/26)) ([b2226d7](https://github.com/manuschillerdev/esphome-elero/commit/b2226d7f47a328a1bebf3da969b715e04cddf5f7))
* hub panel reads freq from radio signal, not hub ([2082a77](https://github.com/manuschillerdev/esphome-elero/commit/2082a7700e41a8fa4b60ce0aeeb0ca8e5512123e))
* **light:** prevent feedback loop in set_rx_state ([f9b5b9b](https://github.com/manuschillerdev/esphome-elero/commit/f9b5b9bffa63b792a9b850a4048be11b6e1b8913))
* **light:** publish final brightness on dimming completion ([ee005a4](https://github.com/manuschillerdev/esphome-elero/commit/ee005a45122cf21d1f1ef3c3baa153d27f00c399))
* millis ([69e14a6](https://github.com/manuschillerdev/esphome-elero/commit/69e14a69ad1847578085ed041adabb4e975b9e8a))
* MQTT stale discovery cleanup, last_seen timestamp sensor, idiomatic HA types ([2c79c20](https://github.com/manuschillerdev/esphome-elero/commit/2c79c20668593bf07467e51289cab26630de47b8))
* **nvs:** add ESPHOME_ENTITY_*_COUNT defines for application.h sized containers ([06f54dd](https://github.com/manuschillerdev/esphome-elero/commit/06f54dd14c8e33c259864d2392a0125b853730d5))
* **nvs:** add USE_COVER/USE_LIGHT defines, remove set_object_id ([1b48a98](https://github.com/manuschillerdev/esphome-elero/commit/1b48a98a78ac40d0133447e9c1d21e55c5cb495b))
* **packet:** zero padding bytes before encryption ([a4160f5](https://github.com/manuschillerdev/esphome-elero/commit/a4160f5660b14787710e8d6af729973dce248739))
* positioning, ui, discovery, stale entity removal ([6844c38](https://github.com/manuschillerdev/esphome-elero/commit/6844c38370c823a3496e24e19c080009e53bca3b))
* **radio:** use cnt-based echo detection instead of src address matching ([33a8a2f](https://github.com/manuschillerdev/esphome-elero/commit/33a8a2f669a947e244f60290b2855f5638aece38))
* replace atomic methods with volatile access in elero.cpp ([b42e377](https://github.com/manuschillerdev/esphome-elero/commit/b42e377f97bb9060f3e6febe5698a88573968f28))
* resolve CI failures (formatting, linting, test configs) ([67f79dd](https://github.com/manuschillerdev/esphome-elero/commit/67f79dd718e6c21844dee53670e884c93df8e721))
* resolve CI failures and remove format enforcement ([39a1f57](https://github.com/manuschillerdev/esphome-elero/commit/39a1f57c1763ce4b3b9a3b915bba140641c2199e))
* restore functionality, fix test setup, remove outdated docs ([868a7a9](https://github.com/manuschillerdev/esphome-elero/commit/868a7a9ec396a819d28d728bd5dd39fb3ea0245a))
* revert payload[8] regression and fix issue [#22](https://github.com/manuschillerdev/esphome-elero/issues/22) — use payload[4] for command byte in all paths ([6ad7569](https://github.com/manuschillerdev/esphome-elero/commit/6ad756938e68358d82238ed62fc40a2492722ab7))
* revert payload[8] regression and fix issue [#22](https://github.com/manuschillerdev/esphome-elero/issues/22) — use payload[4] for command byte in all paths ([6128def](https://github.com/manuschillerdev/esphome-elero/commit/6128def6149ba59dc4690964299b25b2b8fb7446))
* **state-machine:** robustness improvements and test infrastructure ([#12](https://github.com/manuschillerdev/esphome-elero/issues/12)) ([16e2630](https://github.com/manuschillerdev/esphome-elero/commit/16e2630eeed9fa246cd8851015cae14ab7a0c6ac))
* **state-machine:** RX packet rescue and state robustness ([a4c13a4](https://github.com/manuschillerdev/esphome-elero/commit/a4c13a43f2ce96f6ddcd41665765d3dbeac1a929))
* suppress noisy mongoose logs and transient MARCSTATE warnings ([6db7c37](https://github.com/manuschillerdev/esphome-elero/commit/6db7c371c7b6b7fe932e0d4ea5101c620ce98292))
* suppress ruff F401 false positive for sensor import ([ac1b3d1](https://github.com/manuschillerdev/esphome-elero/commit/ac1b3d1fe2ee4a9575b070257d7209df928d7a8e))
* trigger immediate poll when physical remote command is detected ([029e3d2](https://github.com/manuschillerdev/esphome-elero/commit/029e3d2fe803be1810114e8a69d683d7dcbff45f)), closes [#34](https://github.com/manuschillerdev/esphome-elero/issues/34)
* ui ([a9cea6c](https://github.com/manuschillerdev/esphome-elero/commit/a9cea6c46a32a1587e679a94f93af0e407bda610))
* update elero_web for ESPHome 2025.6 API compatibility ([d14a690](https://github.com/manuschillerdev/esphome-elero/commit/d14a69021fb4b59bf67e819a96b4ddf57d641267))
* use volatile instead of std::atomic for ESP8266/RP2040 compat ([c926c98](https://github.com/manuschillerdev/esphome-elero/commit/c926c9849b27ba63c205ce9460d383c27a9c2e14))


### Refactoring

* add SpiTransaction RAII guard and C++17 modernization ([44636fc](https://github.com/manuschillerdev/esphome-elero/commit/44636fcba192d70b9aab16dd05797c33c17acf63))
* apply modern C++ conventions to codebase ([4b0dee4](https://github.com/manuschillerdev/esphome-elero/commit/4b0dee4140814b22bebd99296f914f6d587fd8f1))
* **ci:** focus CI targets on ESP32-S3 and ESP32 ([b756b25](https://github.com/manuschillerdev/esphome-elero/commit/b756b256b78649d507db1be33aa188a5a3ecfc45))
* **ci:** focus CI targets on ESP32-S3 and ESP32 ([d81c6e4](https://github.com/manuschillerdev/esphome-elero/commit/d81c6e40d08e3d89310fcbd2a0704d312cbe082d))
* **command-sender:** extract backoff helper and add unit tests ([2663419](https://github.com/manuschillerdev/esphome-elero/commit/2663419646fc27833194b00358580a576db19735))
* core methods enforce their own invariants ([86aa7ca](https://github.com/manuschillerdev/esphome-elero/commit/86aa7ca70771fa5df1777af554d6f60266af92c8))
* **crud:** unify save/update/enable into single upsert_device ([027b125](https://github.com/manuschillerdev/esphome-elero/commit/027b125bf79316fb199ea78d9c663e96a3ba0b1b))
* **devcontainer:** slim down base image and fix permissions ([176f99e](https://github.com/manuschillerdev/esphome-elero/commit/176f99e2f49315acd396872ad1ecfb0fd18d8c1e))
* extract RadioDriver interface, move CC1101 code to CC1101Driver ([f0006ea](https://github.com/manuschillerdev/esphome-elero/commit/f0006eab97b20639bffe1af8f2f0438fb0e342fd))
* extract shared command handling to CommandSender class ([e35d348](https://github.com/manuschillerdev/esphome-elero/commit/e35d348abaa71d9812ebd73f9f6809d3b775e681))
* **json:** replace manual JSON with ESPHome ArduinoJson abstraction ([100035c](https://github.com/manuschillerdev/esphome-elero/commit/100035c810e8c2dee51bbb463b048b67083d23ce))
* modernize cc1101.h and fix cover position tracking ([c47260b](https://github.com/manuschillerdev/esphome-elero/commit/c47260bf4e07729010ebe735a1e521df3be0b9e7))
* move business logic from web layer to core ([c2f6bb1](https://github.com/manuschillerdev/esphome-elero/commit/c2f6bb1d447f0bf34eeb564a41105a9fbda025b5))
* move sensor ownership from hub to shells, remove standalone sensor platforms ([5b8ef8f](https://github.com/manuschillerdev/esphome-elero/commit/5b8ef8f21e1701c3cd2b17702b3183ac49ec65ad))
* **mqtt:** extract DynamicEntityBase, use ESPHome preferences, consistent lifecycle ([fbfb683](https://github.com/manuschillerdev/esphome-elero/commit/fbfb683dad50b86e3e41ca680d844d8d2e79c780))
* **packet:** add rx_offset namespace and eliminate magic numbers ([88e7eda](https://github.com/manuschillerdev/esphome-elero/commit/88e7edafa8cf604ed286bf88fefafd35c0c74a4d))
* replace dim booleans with DimDirection enum ([54cf536](https://github.com/manuschillerdev/esphome-elero/commit/54cf5366301470802e99a057a5b15c093c425726))
* replace vendored mongoose with build-time download ([cc227ea](https://github.com/manuschillerdev/esphome-elero/commit/cc227eafdfe2a34d62690699b927c270560cbd0f))
* unify field naming across config, code, and API ([2733425](https://github.com/manuschillerdev/esphome-elero/commit/27334253ec63da9b7d332f55877e3014c55393a3))
* unify field naming across config, code, and API ([a62ead6](https://github.com/manuschillerdev/esphome-elero/commit/a62ead6e22e113df49218fdf604b2232c7a6437a))
* use release-please for version management ([2d5272e](https://github.com/manuschillerdev/esphome-elero/commit/2d5272e01441514c50ba384797301ad13a8eee4b))
* web UI uses same code path as Home Assistant ([678713c](https://github.com/manuschillerdev/esphome-elero/commit/678713c8bae553db7133eba2396f6d43df9f905b))
* **web:** migrate frontend state from zustand to Preact signals ([7ff4993](https://github.com/manuschillerdev/esphome-elero/commit/7ff4993b7edf301080ca47bfd42ee38f52aeb6cb))


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
* add RFC-001 for TX race condition fix and naming unification ([1cc6705](https://github.com/manuschillerdev/esphome-elero/commit/1cc6705f2a9730ca2a0baecf86d4bfe4eacd4264))
* add security model documentation ([10d8deb](https://github.com/manuschillerdev/esphome-elero/commit/10d8debe5b5cd6f519bd59843dc61ea08e23a527))
* add tooling section and ESP32 conventions to modern-cpp skill ([e454bbc](https://github.com/manuschillerdev/esphome-elero/commit/e454bbcb5e1cda3b61c4ed8fcb20d1038ce29c43))
* close documentation gaps identified by heartbeat audit ([866df71](https://github.com/manuschillerdev/esphome-elero/commit/866df71d4e4f658687430c51dbdee0ed0be76631))
* rewrite README — concise, English, current ([46d04e2](https://github.com/manuschillerdev/esphome-elero/commit/46d04e28599bf7e9d203dd642d899162ab15f873))
* update CLAUDE.md diagrams for simplified TX state machine ([4deb749](https://github.com/manuschillerdev/esphome-elero/commit/4deb749749cc5091fc7609055aed2f3da99b6c06))
* update CLAUDE.md for Native+NVS mode, core extraction, unit tests ([7bc4cee](https://github.com/manuschillerdev/esphome-elero/commit/7bc4cee3057d8c779a8b759aae1b32ab98e636db))
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
