# Handover — Learn-In API / Backend + Frontend Plumbing

Date: 2026-05-15

## What was done

Implemented the first end-to-end learn-in foundation in three layers:

1. **C++ backend learn-in domain logic**
2. **WebSocket API contract + server wiring**
3. **Frontend transport/store plumbing**

This is intentionally **not** a wizard yet. It exposes backend primitives and API/state so the UI can drive onboarding.

---

## Backend: learn-in manager

### New files
- `components/elero/learn_in_manager.h`
- `components/elero/learn_in_manager.cpp`

### Purpose
Provides a dedicated backend state machine for remote/motor learn-in.

### Public API exposed via `Elero`
In `components/elero/elero.h`:
- `start_learn_in(const LearnInStartRequest &request)`
- `confirm_learn_in_up()`
- `confirm_learn_in_down()`
- `cancel_learn_in()`
- `learn_in_state()`
- `is_learn_in_active()`
- `learn_in()`

### Session states
`LearnInState`:
- `IDLE`
- `PROGRAMMING`
- `WAIT_UP`
- `CONFIRMING_UP`
- `WAIT_DOWN`
- `CONFIRMING_DOWN`
- `COMPLETE`
- `FAILED`
- `CANCELLED`
- `TIMED_OUT`

### Behavior
- uses button-style TX through normal `request_tx()` path
- supports retries/backoff
- has TX timeout handling
- has session timeout handling
- separate from normal cover/light command dispatch

### Important constraint
The backend requires `programming_cmd` to be provided by the caller.

Reason:
- official elero manuals document the **procedure**
- they do **not** document the RF command byte for the `P`/programming action
- that value still must be determined empirically from sniffed traffic

---

## Hub wiring

### Updated files
- `components/elero/elero.h`
- `components/elero/elero.cpp`

### What changed
- `Elero` now owns a `LearnInManager learn_in_`
- `Elero::loop()` now calls `learn_in_.loop(now, this)`

This keeps learn-in in core backend logic, but outside normal runtime device command flow.

---

## WebSocket API contract

### Updated file
- `components/elero_web/frontend/app/asyncapi.yaml`

### Added client → server messages
- `learn_in_start`
- `learn_in_confirm_up`
- `learn_in_confirm_down`
- `learn_in_cancel`

### Added server → client event
- `learn_in_state`

### `learn_in_start` payload
Required:
- `src_address`
- `channel`
- `programming_cmd`

Optional:
- `packets`
- `type2`
- `hop`
- `session_timeout_ms`

### `learn_in_state` event data
Required:
- `state`
- `active`
- `busy`

Optional:
- `src_address`
- `channel`
- `programming_cmd`

### String states used on wire
- `idle`
- `programming`
- `wait_up`
- `confirming_up`
- `wait_down`
- `confirming_down`
- `complete`
- `failed`
- `cancelled`
- `timed_out`

---

## WebSocket server implementation

### Updated files
- `components/elero_web/elero_web_server.h`
- `components/elero_web/elero_web_server.cpp`

### Added handlers
- `handle_learn_in_start_()`
- `handle_learn_in_confirm_up_()`
- `handle_learn_in_confirm_down_()`
- `handle_learn_in_cancel_()`

### Added JSON builder
- `build_learn_in_state_json_()`

### Runtime behavior
- sends `learn_in_state` immediately on WS connect
- broadcasts `learn_in_state` when the backend learn-in state changes
- forwards client learn-in messages into backend APIs

### Internal state cache
`EleroWebServer` now caches last broadcast learn-in state fields so it only pushes updates when values actually change.

---

## Frontend plumbing

### Updated files
- `components/elero_web/frontend/app/src/ws.ts`
- `components/elero_web/frontend/app/src/store.ts`
- `components/elero_web/frontend/app/src/generated/index.ts`

### Added generated types
- `src/generated/LearnInState.ts`
- `src/generated/LearnInStateData.ts`
- `src/generated/LearnInStateEventEnvelope.ts`
- `src/generated/LearnInStartPayload.ts`
- `src/generated/LearnInConfirmUpPayload.ts`
- `src/generated/LearnInConfirmDownPayload.ts`
- `src/generated/LearnInCancelPayload.ts`

### Store additions
In `store.ts`:
- `learnIn` signal
- `onLearnInState(data)`

### WS client additions
In `ws.ts`:
- listens for `learn_in_state`
- added send helpers:
  - `sendLearnInStart(...)`
  - `sendLearnInConfirmUp()`
  - `sendLearnInConfirmDown()`
  - `sendLearnInCancel()`

---

## Tests

### Added
- `tests/unit/test_learn_in_manager.cpp`

### Updated
- `tests/unit/CMakeLists.txt`

### Verified passing manually
- `tests/unit/build/test_learn_in_manager`
- `tests/unit/build/test_device_registry`

The full learn-in manager tests currently cover:
- invalid start rejection
- full happy path
- confirm step ordering
- retry/fail path
- session timeout

---

## Official documentation source captured in skill

Updated skill:
- `.pi/skills/elero-protocol/SKILL.md`

Added official learn-in references from elero manuals for:
- TempoTel 2
- VarioTel 2
- MonoTel 2

Key documented behavior:
- switch circuit breaker off/on after a few seconds
- receiver enters programming mode for about 5 minutes
- press transmitter `P`
- confirm with `UP` / `DOWN`
- warning: multiple receivers on same mains line may all enter programming mode together

---

## What is NOT done yet

### 1. No actual UI onboarding flow yet
There is no visible learn-in wizard/panel in the frontend.

Current frontend status:
- transport and state plumbing exist
- no rendered UX for it yet

### 2. No Home Assistant-facing service integration yet
Only the WebSocket layer was wired.

### 3. `programming_cmd` is not yet known / standardized
The API accepts it, but we still need sniffed real-world data to know the correct byte(s) for the `P` action.

### 4. No persistence/discovery workflow tied to learn-in completion yet
After `COMPLETE`, the client still needs to:
- guide next steps
- discover / assign motor address if needed
- save resulting device config

---

## Recommended next steps

### Next: frontend UI
Add a learn-in panel/wizard in the web app.

Suggested minimum UX:
1. select/create virtual remote address
2. select channel
3. enter `programming_cmd` manually for now
4. show power-cycle instructions
5. call `sendLearnInStart(...)`
6. when state becomes `wait_up`, show “Confirm UP movement” button
7. when state becomes `wait_down`, show “Confirm DOWN movement” button
8. when state becomes `complete`, show success state + next actions
9. always show cancel button while active

### After that: protocol capture
Use real remotes to sniff the actual `P`/programming RF command.
Then:
- define known values in protocol constants
- remove/manual-hide raw `programming_cmd` input from normal UX
- replace with preset per remote family if stable

### Then: onboarding completion flow
After learn-in succeeds, add UI flow for:
- identifying target motor/device
- saving resulting cover/light config to NVS
- optionally prompting for reboot in native mode

---

## Notes / caveats

- The new generated TS files were added manually to match the AsyncAPI contract.
- I ran `pnpm exec tsc --noEmit`, but the generated folder already has a pre-existing project-wide `isolatedModules` issue (`export { Type }` instead of `export type { Type }`) unrelated to this learn-in work. That means TS typecheck is noisy even without these changes.
- Backend/unit test coverage for the new learn-in logic is good enough for this stage.

---

## Files touched

### Backend
- `components/elero/elero.h`
- `components/elero/elero.cpp`
- `components/elero/learn_in_manager.h`
- `components/elero/learn_in_manager.cpp`

### WebSocket / API
- `components/elero_web/elero_web_server.h`
- `components/elero_web/elero_web_server.cpp`
- `components/elero_web/frontend/app/asyncapi.yaml`

### Frontend
- `components/elero_web/frontend/app/src/ws.ts`
- `components/elero_web/frontend/app/src/store.ts`
- `components/elero_web/frontend/app/src/generated/index.ts`
- `components/elero_web/frontend/app/src/generated/LearnInState.ts`
- `components/elero_web/frontend/app/src/generated/LearnInStateData.ts`
- `components/elero_web/frontend/app/src/generated/LearnInStateEventEnvelope.ts`
- `components/elero_web/frontend/app/src/generated/LearnInStartPayload.ts`
- `components/elero_web/frontend/app/src/generated/LearnInConfirmUpPayload.ts`
- `components/elero_web/frontend/app/src/generated/LearnInConfirmDownPayload.ts`
- `components/elero_web/frontend/app/src/generated/LearnInCancelPayload.ts`

### Tests
- `tests/unit/CMakeLists.txt`
- `tests/unit/test_learn_in_manager.cpp`

### Skill / reference
- `.pi/skills/elero-protocol/SKILL.md`

---

## Quick resume prompt

If continuing this work, start with:

> Implement the web UI learn-in panel on top of the existing `learn_in_*` WebSocket API. Use the current backend session states (`programming`, `wait_up`, `wait_down`, etc.) and keep `programming_cmd` configurable for now because the true RF byte is not confirmed yet.
