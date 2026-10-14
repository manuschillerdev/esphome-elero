# Handover Plan — FSM / Command Pipeline Robustness

Date: 2026-05-15
Scope: follow-up review findings around the cover/light FSM command pipeline, queueing, and cross-core delivery robustness.

## Summary

The pure FSM logic (`cover_sm`, `light_sm`) looks sound.

The main risks are **not** in the state transition math itself, but in the **command pipeline around the FSM**:
- queue-full paths
- cancellation semantics
- cross-core completion delivery
- stale group-command metadata
- packet drops under backpressure
- one `millis()` rollover edge case

## Findings to fix

### 1. `clear_queue()` does not truly cancel already-posted RF work
**Severity:** Medium

Once `CommandSender::process_queue()` successfully calls `Elero::request_tx()`, the request already sits in the Core 0 TX queue. A later `clear_queue()` only clears the sender-side logical queue; it cannot retract the already-posted RF request.

**Impact:**
- STOP/retarget can still be preceded by stale movement TX
- logical cancellation != air cancellation

**Files:**
- `components/elero/command_sender.h`
- `components/elero/elero.cpp`
- `components/elero/elero.h`

**Recommended direction:**
- Decide whether this is acceptable-by-design or must be fixed.
- If fixing, introduce a request identity / generation number so Core 1 can ignore stale completions and Core 0 can drop not-yet-started stale requests.
- Keep the fix minimal; do not add bidirectional shared mutable state without a strong reason.

---

### 2. Ignored `enqueue()` failures can silently drop important follow-up commands
**Severity:** Medium

Many registry call sites discard `enqueue()` results via `(void)`.

**Impact:**
Critical reconciliation packets can be lost silently:
- `CHECK` after move
- `CHECK` after STOP
- `RELEASE` after dimming
- per-device `CHECK` after group command

That leaves the FSM in optimistic state without the intended follow-up poll.

**Files:**
- `components/elero/device_registry.cpp`

**Recommended direction:**
- Audit all `enqueue()` calls.
- Classify them:
  - mandatory follow-up
  - opportunistic convenience
- For mandatory follow-ups, log failures at least once with device address + command context.
- Prefer tiny helper wrappers for repeated patterns:
  - `enqueue_check_()`
  - `enqueue_stop_and_check_()`
- Consider whether selected failures should suppress optimistic state changes.

---

### 3. Group-command stale `num_dests` metadata if enqueue fails
**Severity:** Medium-high

`command_group()` mutates `lead.sender.command().num_dests` and `dest_channels[]` before enqueueing the button command. Cleanup currently happens only in `advance_queue_()`. If enqueue fails, stale group metadata remains in the command template.

**Impact:**
A later ordinary button command on the same sender could be emitted as a group packet.

**Files:**
- `components/elero/device_registry.cpp`
- `components/elero/command_sender.h`

**Recommended direction:**
- Fix first.
- Make group metadata transactional:
  - either restore previous template values on enqueue failure
  - or pass group metadata as queue entry state instead of mutating persistent command template
- Add a focused unit test for:
  - failed group enqueue
  - subsequent non-group button enqueue
  - assert no multi-dest packet behavior leaks

---

### 4. `tx_done_queue` overflow can cause false retries / duplicate commands
**Severity:** Medium

If Core 0 cannot post `TxResult`, the sender remains in `TX_PENDING` and later times out as if TX never completed.

**Impact:**
- duplicate air transmissions
- false failure accounting
- retry churn

**Files:**
- `components/elero/elero.cpp`
- `components/elero/elero.h`

**Recommended direction:**
- Reassess queue depth (`4`).
- Verify whether depth `1` would already be sufficient due to single in-flight TX, or whether bursty raw-TX / abort paths justify more.
- Consider making dropped completion a stronger invariant violation (`ESP_LOGE` / stats counter / debug assert in test builds).
- Preserve the “drain tx_done before rx” ordering.

---

### 5. `rx_queue` overflow can drop status packets and desync grounded state
**Severity:** Medium

Core 0 already logs and counts RX drops, but state accuracy degrades if status packets are lost.

**Impact:**
- missed endpoint confirmation
- stale movement / tilt / problem / RSSI
- more optimistic than grounded state

**Files:**
- `components/elero/elero.cpp`
- possibly `components/elero/device_registry.cpp`

**Recommended direction:**
- Measure whether depth `16` is adequate under bursty real hardware conditions.
- If not, prefer controlled mitigation over complexity:
  - larger queue depth
  - tighter RX logging on overflow
  - maybe bounded per-loop drain limits review
- Do **not** add complex dedup/drop heuristics unless measurement proves need.

---

### 6. `CommandSender` retry backoff has a `millis()` rollover edge case
**Severity:** Low

Backoff stores a synthetic timestamp using `now + backoff - INTER_PACKET_MS`, which is vulnerable near `uint32_t` rollover.

**Impact:**
- retry delay may be shortened or skipped around 49-day uptime wrap

**Files:**
- `components/elero/command_sender.h`

**Recommended direction:**
- Replace the synthetic-timestamp trick with an explicit `next_attempt_ms_` or equivalent rollover-safe scheme.
- Keep arithmetic in standard unsigned wrap-safe form.
- Add a rollover-focused unit test.

## Proposed fix order

### Phase 1 — correctness first
1. Fix stale group metadata on failed enqueue
2. Audit/log mandatory `enqueue()` failure paths
3. Review `clear_queue()` cancellation semantics and choose explicit behavior

### Phase 2 — queue delivery robustness
4. Reassess `tx_done_queue` depth / invariant handling
5. Reassess `rx_queue` depth under burst conditions

### Phase 3 — edge-case cleanup
6. Fix `millis()` rollover in retry backoff

## Testing plan

### Unit tests to add/update

#### CommandSender
- failed group enqueue does not leak `num_dests`
- cancellation semantics around posted-but-not-yet-consumed TX request
- retry backoff across `millis()` rollover
- timeout + dropped completion behavior remains deterministic

#### DeviceRegistry
- mandatory follow-up enqueue failure is observable (log/stat/return behavior as decided)
- group command enqueue failure leaves lead sender template in single-dest state
- STOP/retarget behavior when queue is saturated

### Stress / integration checks
- Burst RX packet scenario to evaluate `rx_queue` overflow behavior
- Repeated TX completion pressure to evaluate `tx_done_queue` sizing
- Real-hardware group command under queue pressure

## Constraints / design guardrails

- Keep the pure FSMs (`cover_sm`, `light_sm`) unchanged unless a bug is proven there.
- Prefer simplification over introducing new shared mutable state.
- Keep `DeviceRegistry` the single source of truth.
- Avoid moving business logic into adapters or shells.
- ESP32 only; FreeRTOS queues + atomics are acceptable tools.
- If adding helper abstractions, keep them tiny and local to the command pipeline.

## Suggested review checklist for the next session

1. Confirm whether posted TX cancellation must be best-effort or strict.
2. Patch stale group metadata leak.
3. Replace silent `(void) enqueue(...)` on mandatory follow-ups.
4. Add tests before widening queue behavior changes.
5. Only then tune queue sizes / overflow handling if needed.

## Expected outcome

After this follow-up, the code should have:
- no stale group-command template leakage
- no silent loss of critical follow-up commands
- explicit documented cancellation semantics
- better resilience to cross-core completion/drop edge cases
- rollover-safe retry scheduling
