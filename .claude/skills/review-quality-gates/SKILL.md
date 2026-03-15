# Skill: Review Quality Gates

**Trigger:** Invoke with `/review-quality-gates` before reviewing code — PRs, refactors, or new features. Applicable to any component-based embedded system.

**Purpose:** 14 quality gates distilled from real bugs and design flaws. Each gate maps to a concrete anti-pattern with a specific check action.

---

## Category 1: Structural Integrity

### Gate 1 — Consistent entity lifecycle

All entity types must use the same lifecycle verbs: `activate(config)`, `deactivate()`, `save_config()`, `update_config()`.

**Check:** Grep for ad-hoc setters (`set_address()`, `set_channel()`) on one entity type when others use structured activation. If entity A uses `activate(config)` but entity B uses 5 separate setters, unify them.

```bash
# Find lifecycle methods across entity types — they should match
grep -rn "void activate\|void deactivate\|void save_config\|void update_config" components/
```

### Gate 2 — Observer registration before state-producing actions

State callbacks must be set BEFORE any action that produces state (e.g., `activate()`). Setting callbacks after activation silently drops events during the activation window.

**Check:** In every call site that activates an entity, verify `set_state_callback()` (or equivalent observer registration) appears before `activate()`.

```bash
# Find activate() calls and check what precedes them
grep -n "activate()" components/ -r
# Each should have callback registration above it, not below
```

### Gate 3 — Atomic state transitions (no crash windows)

Never deactivate + reactivate when an in-place `update_config()` suffices. Two NVS writes with a crash window between them can leave the device in an inconsistent state (slot cleared but not yet rewritten).

**Check:** Look for deactivate/activate pairs that could be a single `update_config()`:

```bash
grep -n "deactivate\|activate" components/ -r
# Flag any deactivate() immediately followed by activate() on the same entity
```

### Gate 4 — Single source of truth for events

Exactly one component emits each event type. No dual broadcasting from both a manager and a web server (or both a base class and a derived class).

**Check:** For each event/callback type, grep to confirm exactly one call site emits it:

```bash
# Example: CRUD events should be emitted from one place only
grep -rn "notify_crud_\|on_device_event\|broadcast_event" components/
```

---

## Category 2: Interface Design

### Gate 5 — Names match semantics

Method and field names must reflect what the entity actually is. No `get_blind_address()` on a light entity. No `cover_slots_` array holding lights.

**Check:** Read public API of each class. Flag any name that references a different domain (e.g., "blind" in a light class, "cover" in a generic base).

```bash
# Find naming leaks from prior designs
grep -rn "blind" components/elero/light* components/elero/EleroDynamicLight* 2>/dev/null
grep -rn "cover" components/elero/dynamic_entity_base* 2>/dev/null
```

### Gate 6 — Fewer CRUD verbs = fewer invalid states

Prefer `upsert(config)` over separate `add()` + `update()` + `enable()` when the distinction forces callers to maintain their own state machines about entity existence.

**Check:** Count distinct CRUD verbs exposed by manager interfaces. If callers need to know "does it exist?" before deciding which verb to call, consolidate to `upsert`.

```bash
grep -rn "add_device\|update_device\|remove_device\|upsert_device\|enable_device" components/
```

### Gate 7 — No dead declarations

Every method declared in a `.h` file must have a corresponding `.cpp` definition (or be inline/template). Dead declarations mislead readers and break linking if called.

**Check:**

```bash
# Extract non-inline method declarations from headers, verify they exist in .cpp files
# Focus on newly added or modified headers
grep -n "virtual\|void\|bool\|int\|std::string" components/elero/*.h | grep ";" | grep -v "{" | grep -v "//"
```

---

## Category 3: Code Hygiene

### Gate 8 — DRY parsing/conversion at system boundaries

The same if/else chain or string-to-enum conversion appearing in 3+ places must be extracted to a helper function. Boundary parsing (JSON field extraction, enum mapping) is especially prone to this.

**Check:**

```bash
# Find repeated string comparisons that suggest duplicated parsing
grep -rn '"cover"\|"light"\|"remote"' components/ | grep "if\|else\|==" | head -20
# If the same pattern appears in 3+ files, extract it
```

### Gate 9 — Use framework facilities over hand-rolled equivalents

Use ArduinoJson/ESPHome JSON helpers over custom `json_find_str()`. Use ESPHome Preferences over raw `nvs_open()/nvs_get()`. Use `str_sprintf()` over manual `snprintf` + buffer management.

**Check:** Grep for hand-rolled utilities that duplicate framework features:

```bash
grep -rn "nvs_open\|nvs_get\|nvs_set\|json_find_str\|snprintf" components/
# Each hit should be justified — if a framework equivalent exists, use it
```

### Gate 10 — Common base for shared behavior

If two entity types share 80%+ identical code (same fields, same lifecycle, same NVS pattern), extract a base class with template-method hooks for the 20% that differs.

**Check:** Diff pairs of entity implementations. If they share most methods with only type-specific variations, they need a common base.

```bash
# Compare entity implementations for structural similarity
diff <(grep -n "void\|bool\|uint" components/elero/EleroDynamicCover.h) \
     <(grep -n "void\|bool\|uint" components/elero/EleroDynamicLight.h)
```

---

## Category 4: Separation of Concerns

### Gate 11 — Pure logic separated from framework

Core logic classes must have zero `#include "esphome/..."` directives. They take primitives, return primitives, and are unit-testable on the host without mocking ESPHome.

**Check:**

```bash
# Core files must not include ESPHome headers
grep -rn '#include "esphome/' components/elero/cover_core.h components/elero/light_core.h
# Should return nothing
```

### Gate 12 — Template method for variant behavior

Base classes call virtual hooks; subclasses override. No `if (mode == MQTT) { ... } else if (mode == NVS) { ... }` branches in shared code. Mode-specific behavior belongs in mode-specific subclasses.

**Check:**

```bash
# Find mode-checking branches in shared code — these are design smells
grep -rn "mode == \|hub_mode\|is_mqtt\|is_nvs" components/elero/*.cpp components/elero/*.h
# Hits in base classes or the hub suggest missing polymorphism
```

---

## Category 5: Debuggability

### Gate 13 — Every failure path is observable

Every `return false`, `return`, or early exit in a non-trivial function must be preceded by `ESP_LOGW` or `ESP_LOGE` with enough context to diagnose the issue (address, type, operation name).

**Check:**

```bash
# Find return-false without preceding log
grep -B2 "return false" components/**/*.cpp | grep -v "LOG\|log\|ESP_LOG"
# Each hit needs a log statement added above the return
```

### Gate 14 — Document invariants and limitations

Constraints not enforced by the type system (e.g., "must call X before Y", "max N slots", "reboot required after this operation") must have either:
- A comment at the declaration site, OR
- A runtime assertion/check at the call site

**Check:** Review all public methods that have ordering requirements or preconditions. Verify each has either a doc comment or a runtime guard.

```bash
# Find methods with ordering requirements — check for documentation
grep -rn "must.*before\|requires.*first\|call.*before\|reboot" components/ --include="*.h"
```

---

## How to use these gates

1. **Before reviewing:** Invoke `/review-quality-gates` to load this checklist
2. **During review:** Walk through each gate against the changed files
3. **Prioritize:** Gates 1–4 (structural) catch the highest-severity bugs. Gates 13–14 (debuggability) catch the hardest-to-diagnose issues.
4. **Automate what you can:** The `grep` commands above can be run directly to spot violations mechanically
