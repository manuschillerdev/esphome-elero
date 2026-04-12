# 0x45 Protocol Research Summary

> **Issue:** [#43](https://github.com/manuschillerdev/esphome-elero/issues/43)
> **Branch:** `research/0x45-protocol`
> **Date:** 2026-04-12

---

## What we already know

### 1. 0x45 is a mesh-relayed broadcast — not a command in the traditional sense

- Blinds rebroadcast 0x45 packets **indiscriminately** — non-group-members relay alongside members
- The encrypted 8-byte trailer is **byte-identical** across all relays of the same counter value
- Only the **physical remote** originates 0x45 — blinds relay but never initiate
- Relay count varies 3–7 per trigger and correlates with RF environment, not group membership

### 2. Two distinct contexts for 0x45

| Context | Preceding 0x44? | Channel byte | When |
|---------|-----------------|-------------|------|
| **Sidecar** (small group) | Yes — 3× press + 3× release | `0x00` (group marker) | After every small-group button press (NM, WEST) |
| **Primary** ("all" broadcast) | **No** | `0xFF` (broadcast sentinel) | When the remote's "all" button is pressed |

### 3. Counter relationship

For small groups: `0x45_counter = release_counter + 2` (one counter value consumed by an unseen packet — §7.3 in the investigation doc). For "all": counter increments normally (+1 per emission, press then release).

### 4. 0x44 alone works for TX

Proven by pfriedrich PR155 (merged, production). Multi-dest `0x44` with N 1-byte channel destinations executes group commands without any 0x45 sidecar. **0x45 is not required for our hub to issue group commands.**

### 5. Decoded payload is suspicious

One capture (WEST DOWN cnt=14): `00 00 10 00 00 00 00 c0`

- Bytes `[0..1]` = `00 00` — should be counter-derived if `msg_decode` applies, but they're zero
- Byte `[2]` = `0x10` — this is `ELERO_COMMAND_STOP` in the 0x44 command space, but the trigger was DOWN (`0x40`), so either:
  - `msg_decode` doesn't apply to 0x45 (different encryption or key derivation)
  - The command byte has a different meaning in 0x45 context
  - The decryption produces coincidental-looking output

### 6. Mesh routing mechanics (from 0x6A TX echo analysis)

Two-hop directed mesh routing observed:
- Each relay writes its own address to `bwd` (backward/source of this hop)
- Each relay writes the next-hop target to `fwd`
- Hop byte upper nibble increments by `0x10` per relay (NOT decrement TTL)
- Blinds maintain forwarding tables — they know who the next hop is

### 7. Packet structure for 0x45

Identical header layout to 0x44:
```
[0]     len         = 26 + num_dests
[1]     counter
[2]     msg_type    = 0x45
[3]     type2       = 0x10 or 0x11 (noise — not meaningful)
[4]     hop         = 0x05 (original) / 0x15, 0x25... (relayed)
[5]     sys         = 0x01
[6]     channel     = 0x00 (group) or 0xFF (all)
[7-9]   src_addr    = original sender (immutable across relays)
[10-12] bwd_addr    = last relay (changes per hop)
[13-15] fwd_addr    = next-hop target (changes per hop)
[16]    num_dests
[17+]   dsts[]      = 1-byte channel IDs
[N+1]   0x00        = payload_1
[N+2]   0x04        = payload_2
[N+3..N+10] encrypted trailer (8 bytes)
```

---

## What we don't know

### Critical unknowns

1. **Does `msg_decode` apply to 0x45?** The `00 00` crypto header is the #1 red flag. If 0x45 uses a different key derivation (different multiplier, counter-independent, or no encryption at all), `msg_decode` produces garbage that coincidentally looks clean.

2. **What command byte (if any) lives in the 0x45 encrypted trailer?** We've never systematically compared decoded payloads across UP/DOWN/STOP for the same group. This is the single experiment that answers whether the trailer carries directional information.

3. **Is 0x45 a "commit" signal for edge-case blinds?** Blinds at the edge of direct RF range might only receive the mesh-relayed 0x45 and not the direct 0x44. The 0x45 would then be actual command delivery for those blinds.

### Nice-to-know

4. **What triggers relay behavior?** Is it src_address matching a paired remote, or truly any 0x45 from any source?
5. **What's the unseen counter skip?** One packet per group event is emitted but not captured (counter gap of 2 between 0x44 release and 0x45).
6. **Does "all" via 0x44 multi-dest work?** If we can send `0x44` with `channel=0x00` and `dsts=[1..N]` covering all channels, we might not need 0x45 at all — even for "all" broadcasts.

---

## Reverse engineering plan

### Phase 1: Capture campaign (hardware required)

The `dispatch_packet()` code on `dev` already logs the full 10-byte decoded payload as `"payload":"XX XX XX ..."` for unclassified types (including 0x45). All we need is to flash `dev` and trigger groups.

**6 captures needed:**

| # | Trigger | What to observe | Key comparison |
|---|---------|----------------|----------------|
| 1 | Single blind UP | Negative control — zero 0x45 expected | Baseline |
| 2 | NM group DOWN | 0x45 sidecar payload | — |
| 3 | NM group UP | 0x45 sidecar payload | vs #2: does byte[2] change with direction? |
| 4 | WEST group DOWN | 0x45 sidecar payload | vs #2: does payload change with group membership? |
| 5 | "all" DOWN | 0x45 primary payload | vs #2: does "all" use same format as sidecar? |
| 6 | "all" UP | 0x45 primary payload | vs #5: direction difference in "all" context |

**Analysis protocol:**
- Extract `"payload"` field from every 0x45 log line
- Tabulate: `[counter, direction, group, payload_hex]`
- Focus on:
  - `payload[0..1]`: do they correlate with counter? If yes → msg_decode is valid
  - `payload[2]`: does it correlate with direction (UP=0x20, DOWN=0x40, STOP=0x10)? If yes → 0x45 carries the command
  - `payload[7]`: parity byte — does it validate against the rest?

### Phase 2: Determine encryption validity

**If payload[2] correlates with direction across captures:**
- `msg_decode` works on 0x45 → same encryption scheme
- 0x45 carries a real command → it's a mesh-propagated copy of the 0x44 command
- Document the mapping and close Q1/Q4

**If payload is constant regardless of direction:**
- `msg_decode` does NOT apply to 0x45, or 0x45 doesn't carry a command
- Try: raw encrypted bytes — do THOSE change with direction? If yes, the encryption is different. If no, the trailer is truly content-free (just a "ping" for mesh propagation)
- Try: alternative decryption — different multiplier, or XOR with a different counter base

**If payload is random/garbage across captures:**
- msg_decode produces nonsense → 0x45 uses a completely different crypto scheme
- Document as opaque and move on

### Phase 3: Mesh lifecycle analysis

From the same captures, also answer:
- **Relay latency:** timestamp delta between original (hop=0x05) and last observed relay
- **Relay consistency:** do the same blinds relay every time, or is it stochastic?
- **Non-member relay rate:** what fraction of relays are from blinds NOT in the group?
- **Two-hop propagation:** do we ever see hop=0x25 (two relays deep) for 0x45, like we saw for 0x6A?

### Phase 4: TX experiment (if Phase 2 shows 0x45 carries a command)

1. Build a `build_0x45_packet()` using the discovered payload format
2. Test "all" via 0x45 with `channel=0xFF` and `command=0x00` (CHECK — safe)
3. Compare blind responses vs "all" via 0x44 multi-dest
4. If both work equally: prefer 0x44 (simpler, no mesh relay needed from our hub)

### Phase 4-alt: TX experiment (if Phase 2 shows 0x45 is empty/opaque)

1. Test "all" via `0x44` multi-dest with `channel=0x00` and `dsts=[1..7]`
2. If that works: 0x45 is purely a mesh-propagation mechanism and we never need to generate it
3. Document 0x45 as "relay-only protocol, not needed for hub TX"

---

## How the mesh likely works (hypothesis)

Based on §4.5 (two-hop directed routing for 0x6A) and §4.2–4.4 (0x45 broadcast flooding):

### Two distinct mesh modes

1. **Directed routing (0x6A, 0xCA):** blinds maintain forwarding tables. Each relay knows the next hop toward the destination. `bwd` = "I am", `fwd` = "going to". Hop counter increments. Classic multi-hop routing.

2. **Broadcast flooding (0x45):** every blind in range that has the originating remote in its pairing table rebroadcasts once. No forwarding table needed — just "heard it, repeat it." The encrypted trailer is preserved verbatim (blinds don't decrypt-then-re-encrypt). Simple store-and-forward flooding with a single-hop TTL (we've only seen hop=0x15, never 0x25 for 0x45).

### Why two modes?

- **Directed routing** is efficient for point-to-point (hub → one blind, blind → hub). Minimizes RF congestion.
- **Broadcast flooding** is the right strategy for "make sure every blind in the mesh hears this." A group command needs to reach N blinds, some of which may be out of direct range. One flood round reaches every blind within 2 hops.

### Why 0x45 exists alongside 0x44

Best hypothesis: **range extension.** The remote sends 0x44 directly (3× retransmit, no mesh relay). Blinds in direct range execute immediately. Then the remote sends 0x45, which gets mesh-relayed. Blinds out of direct range but within one hop of a relaying blind receive the command via the mesh. Belt-and-suspenders: 0x44 for low-latency direct delivery, 0x45 for high-reliability mesh delivery.

For "all" broadcasts, the remote skips 0x44 entirely and goes straight to 0x45 — mesh flooding is the efficient strategy when the audience is "everyone."

### Forwarding table population

Unanswered, but likely populated during the pairing ("Anlernen") process. When a blind is paired to a remote, it probably exchanges neighbor discovery information with nearby blinds. This would explain why only paired blinds relay (if that's the gate) and why the mesh topology is relatively stable.

---

## Key files

| File | What's there |
|------|-------------|
| `docs/ELERO_GROUP_INVESTIGATION.md` | Complete investigation journal (§1–9) |
| `docs/plans/PR3_EVALUATE_0x45.md` | PR3 plan |
| `.claude/skills/elero-protocol/SKILL.md` | Protocol reference |
| `components/elero/elero.cpp:600+` | dispatch_packet() — 0x45 payload logging |
| `components/elero/elero_packet.cpp` | parse_packet() implementation |
| `components/elero/elero_protocol.h` | msg_decode() encryption/decryption |
