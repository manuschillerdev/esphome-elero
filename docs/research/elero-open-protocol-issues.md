# Open Elero protocol issues: evidence and remaining work

Checked 2026-10-14 against local HEAD `c0499e373d9d28fd66ac8e632a41dd581c9f968e`.
GitHub issue bodies and comments were read with `GH_TOKEN="" gh issue view`.
Neither [#37](https://github.com/manuschillerdev/esphome-elero/issues/37) nor
[#43](https://github.com/manuschillerdev/esphome-elero/issues/43) had comments at
the time of this review. This note distinguishes source facts, historical
observations, and experiments still needed; it does not claim hardware validation.

## Existing branches checked first

| Issue | Existing local branch / commit | What it already provides | Remaining gap |
| --- | --- | --- | --- |
| #37 | `pi/20260523-learn-in-virtual-remote`, `7edaa9bc0013aba147c698037bf2299d30108694` | WebSocket learn-in operations, a rendered panel, and a hardware-derived virtual address. This commit is an ancestor of HEAD. | The original branch used a manually entered programming command. HEAD supersedes it with canonical single-channel P TX in `8e565ac0436503e6358bf0e0c161d4e9f0a44990`. Receiver acceptance still requires a physical check. |
| #43 | `research/0x45-protocol`, `edc8ac616d5869beae35217d000759ae403643d6` | Additional logging and `docs/0x45_RESEARCH_SUMMARY.md`. | Research preparation, rather than a complete protocol specification or validated TX implementation. Its zero-key argument is incorrect; see below. |
| #43 | `docs/group_investigation`, `84f680cd07b9804c98988b7bed0bc89a8dc5f89d` | `docs/ELERO_GROUP_INVESTIGATION.md` with exact historical raw captures, plus research plans. | This document is absent from the current checkout, but its capture evidence remains available in Git. Several interpretations in it are hypotheses, and its old address-width rule predates P packet support. |

The two existing learn-in/research worktrees were clean when inspected. None of
these branches was modified. Branch evidence was read with `git show`,
`git log --all`, `git branch --contains`, and focused comparisons against HEAD.
The branch names and commit IDs identify the reviewed versions, even if the
branches later move. [Learn-in implementation commit](https://github.com/manuschillerdev/esphome-elero/commit/8e565ac0436503e6358bf0e0c161d4e9f0a44990)

## #43: results obtainable from existing captures

### The zero crypto header is expected

`msg_decode()` first substitutes nibbles, then subtracts the arithmetic offset
from bytes 0 and 1. It then passes those recovered bytes as the XOR key to
`xor_2byte_in_array_decode()`. That function XORs **all four pairs, including
the first pair**. Consequently, decoded bytes 0 and 1 are each XORed with
themselves and become zero by construction. This applies to ordinary `0x44`
packets as well as `0x45`. A decoded `00 00` header is not evidence of a different
cipher or failed decryption. The key must be inspected before that XOR operation.
[Decoder and XOR implementation](../../components/elero/elero_protocol.h)

The existing issue and the research branch both treat those zeros as suspicious.
That premise can be corrected without any new RF experiment.
[Issue #43](https://github.com/manuschillerdev/esphome-elero/issues/43)

### Decode of the recorded byte sequences

The following values were recomputed from the raw bytes in section 4 of
`git show docs/group_investigation:docs/ELERO_GROUP_INVESTIGATION.md`, using a
Python transcription of the current nibble substitution, nibble subtraction,
and XOR functions. The command ran through `uv run --no-sync python`; no test
files or firmware were changed. The table contains distinct logical packets,
not each identical relay copy. [Current algorithm](../../components/elero/elero_protocol.h)

| Recorded context | Type / counter | Encrypted eight bytes | Decoder output |
| --- | --- | --- | --- |
| Single-channel UP control | `44 / D3` | `FA 10 95 67 DA 10 76 1B` | `00 00 20 00 00 00 00 40` |
| Single-channel release control | `44 / D4` | `40 0A E7 F5 C0 9A 4B 36` | `00 00 00 00 00 00 00 00` |
| NM group DOWN press | `44 / DF` | `09 B8 F8 93 33 F8 99 3F` | `00 00 40 00 00 00 00 C0` |
| NM group release | `44 / E0` | `15 E2 61 24 16 42 5A CE` | `00 00 00 00 00 00 00 00` |
| NM group sidecar | `45 / E2` | `E7 11 0D 66 2B 11 E0 15` | `00 00 10 00 00 00 00 40` |
| WEST group DOWN press | `44 / E7` | `8F 97 B3 DD 78 BB DF B0` | `00 00 40 00 00 00 00 40` |
| WEST group release | `44 / E8` | `CF 65 23 A1 48 56 CF 1A` | `00 00 00 00 00 00 00 00` |
| WEST group sidecar | `45 / EA` | `A1 4C 86 CE A1 EC 65 44` | `00 00 10 00 00 00 00 C0` |
| All-channel primary emission | `45 / D6` | `6C C4 62 EC 54 CE 1C C2` | `00 00 40 00 00 00 00 40` |
| Next all-channel emission | `45 / D7` | `9C 8B E2 00 B4 77 0C 7D` | `00 00 10 00 00 00 00 40` |

All capture bytes and physical trigger annotations come from that historical
journal; the decoder output is a new offline calculation. The journal labels
the all-channel emissions only as likely press/release. The decoded `0x40`
does not independently establish which physical button was pressed.

**Inference:** the existing decoder produces structured, context-dependent
payloads for the recorded `0x45` packets, rather than the alleged invalid zero
key. It produces `0x10` for both recorded sidecars even though the preceding
physical action was DOWN. Therefore `0x10` in a sidecar must not automatically
be described as the user's STOP action. The all-channel primary packet decodes
to `0x40`, while its next emission decodes to `0x10`; that alone does not establish
release, commit, or stop semantics.

### Header shape already known for these samples

For the recorded selector-list envelope with N destinations:

| Offset | Recorded field |
| --- | --- |
| 0 | Length excluding its own byte: `26 + N` |
| 1 | Counter |
| 2 | `0x45` |
| 3 | Observed `0x10` or `0x11`; meaning unresolved |
| 4 | Observed original `0x05`, observed relays `0x15` |
| 5 | `0x01` in these samples |
| 6 | `0x00` for the recorded groups, `0xFF` for the recorded all-channel trigger |
| 7–9, 10–12, 13–15 | Source, backward, forward address fields |
| 16 | N |
| 17 through `16 + N` | N one-byte channel selectors |
| `17 + N`, `18 + N` | Recorded prefix `00 04` |
| `19 + N` through `26 + N` | Eight encrypted bytes |

This is the layout of the available captures, not a claim that every firmware
or programming context uses identical prefix/header values. Current code
already explicitly classifies `0x44`, `0x45`, and `0x70` as one-byte selector
envelopes; `0x70` is the counterexample to the old `type > 0x60` addressing rule.
[Envelope classifiers](../../components/elero/elero_packet.h)

The historical NM sample includes relays from receivers outside NM, but all
identified relays belong to the same physical remote's paired population.
It does not test an unpaired receiver or prove unconditional forwarding.
The raw bytes preserve the encrypted trailer across the observed relay copies.
Counter gaps do not prove that an unseen packet was emitted: that is one possible
explanation, not an observation. Source: journal sections 4.2–4.4 and 7.3,
retrievable from `docs/group_investigation` as above.

### Hardware experiments still needed

Use the historically referenced LilyGO T-Embed / CC1101 hub, or an equivalent
ESP32/CC1101 receiver whose configuration is verified against a known physical
remote. Record remote/motor models, radio frequency/configuration, group
membership and paired-source relationships. Capture original on-air bytes,
length, CRC, RSSI/LQI, timestamps, and the manually recorded physical trigger.
Do not rely exclusively on fields already interpreted by the decoder.
[Issue capture plan](https://github.com/manuschillerdev/esphome-elero/issues/43)

1. Repeat the issue's six controls: single-channel UP, same-group DOWN/UP,
   different-group DOWN, and all-channel DOWN/UP. Add STOP and repeated trials
   for each group and all-channel context.
2. Compare all eight decoded bytes and the raw trailers across direction,
   group membership, original emissions, and relay copies. Zero bytes 0 and 1
   cannot be used as a decryption-validity test.
3. To test the forwarding eligibility question, compare receivers paired to
   the emitting remote with receivers paired only to a different remote;
   keep their RF visibility and membership explicitly recorded.
4. To test whether the sidecar provides additional delivery, observe a receiver
   with measured weak direct reception and a known reachable relay. Compare
   normal remote operation with a validated `0x44` group transmission. Missing
   packets at one monitoring hub do not prove a receiver missed them.
5. Compare `0x44` commands targeting all known selectors with the physical
   remote's all-channel operation. Record every receiver's resulting state;
   do not assume direct group TX preserves mesh coverage.

The current builder can encode multi-destination `0x44`, and the earlier
[#41](https://github.com/manuschillerdev/esphome-elero/issues/41) reports that
this worked without a sidecar. That observation does not settle #43's range,
relay-policy, or complete all-channel TX requirements.
[Group packet builder](../../components/elero/elero_packet.cpp)

### Safe follow-up improvements

- Correct the zero-header premise in the issue and older research notes.
- Preserve the historical raw captures and this decoded comparison when
  consolidating documentation; update `PACKET_STRUCTURE.md`'s old threshold
  rule to match the explicit current envelope classifier.
- If structured research logging is restored, carry raw selector/header/trailer
  bytes and decoder output without asserting sidecar semantics. The old research
  branch's final RF log format has seven `%s` slots but five string arguments,
  and it uses the obsolete address threshold; port the useful evidence fields
  to the current architecture instead of copying that code unchanged.
- Keep unvalidated `0x45` TX and context-specific semantics out of normal
  command generation until the missing experiments are completed.

These suggestions are grounded in `git diff HEAD...research/0x45-protocol --
components/elero/elero.cpp`, the current [RF dispatch](../../components/elero/elero.cpp),
and the current [packet reference](../PACKET_STRUCTURE.md).

**Disposition:** #43 has meaningful offline answers now, including an actual
decode of the existing all-channel samples. Its complete protocol, relay
eligibility, and hardware TX deliverables remain unresolved. Keep it open.

## #37: standalone remote learn-in

### Official procedure and implemented RF path

The manufacturer's TempoTel 2 manual describes power cycling the installed
receiver, about five minutes of learn readiness, a brief P action, then UP
and DOWN within one second of their respective movement starts. The blind
must stop after the DOWN confirmation; otherwise the procedure is repeated.
Receivers sharing the supply can enter learn readiness together. These are
receiver/user requirements; the manual does not specify RF bytes.
[Official manual, printed pages 26–27](https://www.elero.com/en/downloads-service/downloads/?cHash=19416ea19b003d4510544ed8c8638c8f&tx_avelero_downloads%5Baction%5D=download&tx_avelero_downloads%5Bdownload%5D=324)

HEAD already builds canonical `0x70` / command `0x80` single-channel P packets
with `type2=0x10`, `hop=0x00`, a one-byte selector, and prefix `00 03`.
The project protocol skill records the empirical MonoTel/TempoTel/VarioTel
captures behind that choice, while leaving group and targeted P variants RX-only.
[Packet builder](../../components/elero/elero_packet.cpp),
[project protocol evidence](../../.pi/skills/elero-protocol/SKILL.md)

`LearnInManager` queues P, then user-triggered `0x44` UP and DOWN, with retries,
cancellation and a session deadline. The WebSocket server forwards those
operations and the UI renders them. The default source identity is the ESP32
station MAC's lower three bytes, with a nonzero fallback; users can override it.
This is deterministic, but truncating a MAC address is not proof of global
24-bit address uniqueness.
[Learn-in manager](../../components/elero/learn_in_manager.cpp),
[server integration and identity derivation](../../components/elero_web/elero_web_server.cpp),
[rendered panel](../../components/elero_web/frontend/app/src/components/learn-in-panel.tsx)

The issue's claim that the programming packet is wholly unknown is therefore
outdated relative to local HEAD. The historical handover's manual
`programming_cmd` contract is also superseded by the canonical P implementation.
[Issue #37](https://github.com/manuschillerdev/esphome-elero/issues/37),
[historical handover](../../.pi/reference/HANDOVER_LEARN_IN_API_2026-05-15.md)

### What completion actually means today

The manager advances from `CONFIRMING_DOWN` to `COMPLETE` on successful local TX
callbacks. It does not process receiver RF acknowledgments or persist a new motor
configuration in that transition. There is no source evidence here that the
receiver accepted the new identity simply because the transport callback succeeded.
The panel explicitly leaves discovery and saving configuration as a next step.
[Completion transition](../../components/elero/learn_in_manager.cpp),
[completion UI](../../components/elero_web/frontend/app/src/components/learn-in-panel.tsx)

The session counter starts at one and is in memory. The issue itself reports
nonsequential counters accepted by its tested receivers; persistent per-blind
counters should not be invented as a universal protocol requirement without
contradictory receiver evidence. Persisting the chosen identity and device channel
is independently necessary for subsequent configured commands.
[Session initialization](../../components/elero/learn_in_manager.cpp),
[issue observations](https://github.com/manuschillerdev/esphome-elero/issues/37)

### Hardware acceptance needed to close #37

Use one identified receiver and a hub with CC1101 or another already validated
radio backend. Choose a nonzero virtual source that is not an existing physical
remote's address in that installation; record source, selected channel, firmware
revision, motor model, frequency and radio configuration.

1. Isolate the intended receiver for the manual's power-cycle procedure.
2. Start hub learn-in using its virtual identity; capture emitted P/UP/DOWN and
   all receiver responses, with movement timing annotations.
3. Trigger the UP and DOWN confirmations within the manual's one-second windows,
   and observe the receiver's final stop. A `COMPLETE` UI state alone is insufficient.
4. Discover and save the receiver address with the same virtual source/channel
   through the existing device configuration workflow.
5. Verify normal UP, DOWN and STOP from that identity without pressing a physical
   remote, then reboot the hub and verify control again.
6. Export and restore the saved configuration and confirm the source/channel
   association is retained. Record receiver-specific failures rather than
   generalizing across all Elero products.

Safe documentation/UI improvements are to state the one-second confirmation
windows precisely and describe completion as transmitted steps pending observed
receiver acceptance. The present panel's generic “jogs or reacts” and “first
confirmation accepted” wording omits that distinction.
[Official procedure](https://www.elero.com/en/downloads-service/downloads/?cHash=19416ea19b003d4510544ed8c8638c8f&tx_avelero_downloads%5Baction%5D=download&tx_avelero_downloads%5Bdownload%5D=324),
[current panel](../../components/elero_web/frontend/app/src/components/learn-in-panel.tsx)

**Disposition:** local branches already contain the major software implementation
for #37. Keep the issue open for a recorded successful standalone learn-in,
normal commands after reboot, and the saved configuration acceptance steps;
do not reimplement the existing branch or claim receiver acceptance from TX alone.
