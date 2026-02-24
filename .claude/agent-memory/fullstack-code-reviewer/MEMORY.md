# Code Reviewer Agent Memory — esphome-elero

See detailed notes in: architecture.md, recurring-bugs.md, conventions.md

## Key architectural facts
- Hub (Elero) dispatches to covers via EleroBlindBase, to lights via EleroLightBase (parallel abstract interfaces)
- elero_web component lives in `components/elero_web/`, NOT `components/elero/elero_web_server.*`
- Light platform added in addition to cover; same RF command queue / retry pattern
- `EleroBlindBase` has grown a large web-API surface (13+ virtual methods) — now a concern

## Critical known bugs (as of 2026-02-24 full review)
- `send_command()` writes payload[8] for cmd_byte but cover/light use payload[4]: **command mismatch for runtime blinds**
- `ELERO_STATE_BOTTOM_TILT` and `ELERO_STATE_OFF` share value 0x0f; `ELERO_STATE_ON` = 0x10 overlaps state range
- `wait_tx_done()` blocks on `received_` flag — ISR writes bool, main loop can deadlock if TX GDO0 never fires
- Blocking spin-loops (wait_rx, wait_idle, wait_tx, wait_tx_done) run inside send_command which is called from loop() — starves scheduler up to 40ms per call
- JSON string-building via `std::string +=` in web handlers can cause heap fragmentation under load

## Files frequently reviewed together
- elero.h + elero.cpp + cover/EleroCover.cpp (command queuing, protocol)
- elero_web_server.cpp + elero.h (API surface, CORS)
- cover/__init__.py + elero/__init__.py (CONF_ constants, code-gen)

## Confirmed conventions
- Private members: trailing underscore
- Constants: ELERO_ prefix + UPPER_SNAKE_CASE (defined as `static const`, not `#define`)
- Platform sub-components: DEPENDENCIES = ["elero"]
- Command counter wraps 0xff → 1 (never 0)
- Poll stagger: (cover_index * 5000) ms offset
