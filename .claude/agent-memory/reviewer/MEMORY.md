# Reviewer Agent Memory

## ESPHome Light Platform: write_state() is ALWAYS deferred

`LightCall::perform()` NEVER calls `write_state()` synchronously. Even the
"immediate" path (`set_immediately_()`) only calls `schedule_write_()`, which
sets an internal `next_write_` flag. The actual `write_state()` call happens
on the next `loop()` iteration. Any flag-based feedback-loop suppression that
is raised and cleared within `perform()`'s call stack will have no effect.

Correct pattern: raise the guard flag before `call.perform()`, then consume
(clear) it inside `write_state()` itself on the first suppressed call.

See: `esphome/components/light/light_state.cpp` — `set_immediately_()` calls
`schedule_write_()`, not `write_state()` directly.

## Constant collision: ELERO_STATE_BOTTOM_TILT == ELERO_STATE_OFF == 0x0f

Defined in `components/elero/elero.h` lines 54-55. Any code that branches on
`ELERO_STATE_OFF` will also fire on bottom-tilt motor responses. The light
platform is affected. Cover platform handles it in a `case` fallthrough
(`case ELERO_STATE_BOTTOM_TILT:`) which is correct for covers but ambiguous
for lights. Flag this whenever reviewing new platform consumers.

## Queue overflow: EleroLight missing ELERO_MAX_COMMAND_QUEUE guard

`EleroCover` guards every `commands_to_send_.push()` with a size check.
`EleroLight` (as of first review) had NO such guard on any of its 7 push
sites. Repeated `write_state()` calls (e.g. HA brightness slider) will grow
the queue unboundedly. Always verify this guard is present when reviewing
light or any new platform component.

## state_ pointer assignment in write_state() is fragile

`EleroLight` assigns `this->state_ = state` on the first `write_state()` call
rather than in `setup()`. Any code path reading `this->state_` before the
first write will dereference null. Flag this pattern in future light platform
reviews. Correct approach: obtain and store the LightState pointer in
`setup()`.

## Files frequently reviewed together

- `components/elero/light/EleroLight.h` + `EleroLight.cpp` + `light/__init__.py`
- When reviewing light: also check `components/elero/elero.h` for constant
  values and `elero.cpp` for how `set_rx_state()` is dispatched in
  `interpret_msg()`.

## Project-wide patterns confirmed

- `EleroCover` is the reference implementation for queue management, retry
  logic, counter increment, and state-restore in `setup()`.
- The `ignore_write_state_` / feedback-loop suppression pattern is new to
  EleroLight; EleroCover does not need it because `set_rx_state()` calls
  `publish_state()` directly without going through a LightCall.
