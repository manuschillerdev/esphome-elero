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

## USB transmitter stick and Volker Bier audit, 2026-11-04

This audit uses the original Elero USB stick, firmware `01.10`, through its
FT232 serial interface at 38400 baud, 8N1. Channels 1 and 2 are paired.
The owner confirmed that the paired motors are not available at the test
location. Thus these results verify the USB interface with unavailable
receivers. They do not verify RF packets, delivery, movement, or slat behaviour.

### Primary sources and exact scope

- [Official Transmitter Stick API package](https://www.elero.com/en/downloads-service/downloads?cHash=7b1affb21fe4ad416d9728207cca3617&tx_avelero_downloads%5Baction%5D=download&tx_avelero_downloads%5Bcontroller%5D=Download&tx_avelero_downloads%5Bdownload%5D=1432).
  The one-page `Easy Control Transmitter Stick.pdf` defines `4A` CHECK,
  `4C` SEND, `4E` INFO, `4B` CONFIRM, and `4D` ACK. The sum of all frame
  bytes modulo 256 is zero. Frame size is the length byte plus two.
  The five documented key bytes are `10`, `20`, `24`, `40`, and `44`.
  The document says that the stick has no command queue and can replace a
  pending command when a new command arrives. Its timeouts are one second
  for CHECK and four seconds for SEND/INFO.
- The same package contains `Sombrero.exe`, SHA-256
  `971ef664da50fb7411d3882e6415802dc533e73c0e2cf86648272405927954f1`.
  Static .NET IL inspection supplies the native requests below. The executable
  was not run. No firmware image or implemented firmware read operation was
  found in this package.
- [Bier's binding at the inspected revision](https://github.com/vbier/openhab-addons/tree/9aab0f7d12dde8002a06aa4ef160122415a54071/bundles/org.openhab.binding.elerotransmitterstick).
  This is Java host software. It uses the Easy Control interface. It does
  not implement the RF encoder, extract stick firmware, or define `21`/`41`.

The Easy Control matrix covers INFO on all 15 single-channel masks, empty
mask, paired group `0003`, mixed paired/unpaired mask `0005`, and all-channel
mask `7FFF`. It also covers all five documented keys plus experimental keys
`21` and `41`, each on masks `0001`, `0002`, and `0003`, with STOP between
tests. It includes a bad-checksum CHECK, recovery CHECK, and INFO interruption.
This is a defined matrix of known operations, not every possible bit pattern.
No programming, pairing deletion, bootloader entry, clock setting, firmware
write, or arbitrary opcode scan was done.

### Easy Control results

| Test | Observed USB result |
| --- | --- |
| CHECK before and after the matrix | `AA 04 4B 00 03 04`: paired channels 1 and 2 |
| Version before and after | Same 88-byte reply, firmware `01.10`, same channel metadata |
| INFO on either paired channel | `AA 05 4D 00 00 00 04`, after 3.41–3.52 seconds |
| INFO on unpaired channels 3–15 | No reply within 4.25 seconds |
| INFO on mask `0005` | Same zero-channel, no-information reply |
| INFO on `0000`, `0003`, or `7FFF` | No reply within 4.25 seconds |
| Each of seven tested keys on either paired channel | Same zero-channel, no-information reply |
| Each of seven tested keys on combined mask `0003` | No reply within 4.25 seconds |
| CHECK with deliberately wrong checksum | No reply; next valid CHECK succeeds |
| INFO channel 1, then INFO channel 2 about 153 ms later | One zero-channel failure reply, about 3.41 seconds after the second request |

There are 65 completed Easy Control cases and 68 transmitted requests,
including the two interruption requests and final STOP. All 36 received
frames have valid checksums. STOP cleanup cases are included in these counts.
The raw transcript retains requests, received chunks, complete frames, and
monotonic timestamps. No reply means no reply in the stated observation
window, rather than proof that nothing was transmitted over RF.

The zero channel mask is significant: the stick loses the requested channel
in this Easy Control failure reply. It cannot be assigned to a device from
the reply alone. `21`/`41` producing this same reply does not prove that the
firmware implements those keys or sends those exact RF command bytes.

### Extended serial interface

The static `Form1.SendInfo` method at RVA `0xFBA0` and `SendDrive` at
`0xFC14` use USB opcode `45`, followed by the two-byte channel mask and eight
data bytes. These are USB opcodes, not RF envelope types with the same values.
The exact INFO data are `02 04 00 00 00 00 00 01`. The ordinary command data
are `00 04 <key> 00 00 <parameter> 00 01`. The application's UP/DOWN buttons
use parameter `01`, STOP/TILT use `00`, and INTERMEDIATE uses `04`.
These field values are reconstructed from the application; meanings beyond
the known button paths remain unverified.

The `bAskTime_Click` method at RVA `0x10324` sends `AA 02 44 10`.
The stick responds with `AA 08 43 01 01 0D 00 21 05 D6`. The application's
`MessageReceiveTime` formats that as 2013-01-01 00:33:05. This is the stick's
reported clock value at the time of this request, not a firmware build date.

The extended INFO probes produced the following results:

| Requested channel mask | Reply within 4.25 seconds |
| --- | --- |
| `0001` | `AA 04 47 00 01 0A`: native NACK, channel 1 |
| `0002` | `AA 04 47 00 02 09`: native NACK, channel 2 |
| `0004` (unpaired channel 3) | No reply |
| `0003`, `0005`, `7FFF`, or `0000` | Native NACK with channel mask `0001` |

`MessageReceiveNACK` in the official application identifies response opcode
`47` as NACK. Thus native single-channel failure replies retain the requested
channel, while Easy Control reports no information with channel mask zero.
For the tested group and zero masks, the native failure reply identifies only
channel 1 within that observation window. The longer follow-up below finds
a second group reply after that window. A zero-mask request gives no reply in
the clean isolation controls below. Do not assign a late group reply to a new
request merely because it arrived after that request.

The native matrix also tests all five ordinary keys on masks `0001`, `0002`,
and `0003`, with STOP cleanup. Each command on `0001` returns channel-1 NACK;
each command on `0002` returns channel-2 NACK; each command on `0003` returns
channel-1 NACK. There are 40 native cases/requests and 39 received frames.
These per-command observations use a 4.25-second window; they do not include
all later replies that can follow a group request. These are capture windows,
not a guarantee that each captured frame belongs to the immediately preceding
request. The later isolated controls address that limitation.
All received native frames have valid lengths and checksums, including a reply
that arrived as two serial chunks. Version and paired-channel data match the
initial values.

Together, the two matrices contain 105 cases, 108 requests, and 75 valid reply
frames. Evidence and exact probe scripts are saved here:

- [Machine-readable summary](usb-stick-2026-11-04/summary.json)
- [Easy Control raw traffic](usb-stick-2026-11-04/matrix-session.jsonl)
- [Native raw traffic](usb-stick-2026-11-04/native-session.jsonl)
- [Easy Control probe snapshot](usb-stick-2026-11-04/probe_matrix.py)
- [Native probe snapshot](usb-stick-2026-11-04/native_matrix.py)
- [Decoded version/channel metadata](usb-stick-2026-11-04/decoded-stick-info.json)

The scripts are records of this hardware session. They select the owner's
specific USB serial ID, write logs under `/tmp/elero-official-api`, and send
movement commands. Do not run them as passive inspection tools. Their only
external Python dependency is `pyserial`; it was supplied with transient
`uv run --no-project --with pyserial` environments.

### What Bier did

| Date | Primary evidence | Implemented action or reported result |
| --- | --- | --- |
| 2017-09-01 | [Initial PR #2659](https://github.com/openhab/openhab-addons/pull/2659) | Added a serial binding with channel discovery, five movement keys, and status polling. Merged 2019-01-14. |
| 2017-11-17 | [Bier's review reply](https://github.com/openhab/openhab-addons/pull/2659#discussion_r151664666) | Reports that Elero confirmed a multi-channel firmware bug and promised a fix. His workaround splits commands into one serial operation per channel. |
| 2019-01-07 | [Firmware test report](https://github.com/openhab/openhab-addons/pull/2659#issuecomment-451960731) | Reports testing several preview firmwares from Elero, with worse results. No firmware attachment is present in this report. |
| 2019-01-07 | [Group removal](https://github.com/openhab/openhab-addons/pull/2659#issuecomment-452043250) | Removed the binding's group-specific code before merge. Per-channel dispatch remains in `TransmitterStick.java`. |
| 2019-01-08 | [Percentage explanation](https://github.com/openhab/openhab-addons/pull/2659#discussion_r246033605) | Explains that 25% and 75% are host mappings for intermediate and ventilation presets. They are not measured positions or stick percentage commands. |
| 2020-12-28 / 31 | [PR #9562](https://github.com/openhab/openhab-addons/pull/9562) | Reports channel-15 INFO returning channel-1 NO_INFORMATION and overwriting channel 1. Added command/reply channel matching; merged December 31. |

The inspected `TransmitterStick.java` splits both `executeCommand()` and
`requestUpdates()` into single-channel operations. Its host queue gives timed
commands priority 30, ordinary commands 20, fast INFO 10, and periodic INFO 0.
It removes superseded queued operations for the same channel list. It schedules
another INFO two seconds after a reply that reports movement; otherwise an INFO
request schedules the next periodic INFO. The serial layer waits at most one
second for CHECK and four seconds for other operations.

`Response.isResponseFor()` compares the reply's decoded channels with the last
command's channels. Thus Bier's current binding would reject the zero-channel
failure replies captured here. This is a host-side guard against state corruption;
it does not repair stick firmware or establish successful RF delivery.

### Exact historical code and offline execution

The 2017 review comment retains an `original_commit_id` of
`6f2b385eca948ab9b65f89853f606f3f75b44195`. Its source tree is still readable,
including the former `EleroGroupHandler.java`. Its group handler stores one
status per channel. If all statuses match, it publishes that common status;
otherwise it publishes NO_INFORMATION. The group handler does not implement
an RF group command. The transport worker already splits commands and INFO
requests into individual channel operations.
[Historical group handler](https://github.com/openhab/openhab-addons/blob/6f2b385eca948ab9b65f89853f606f3f75b44195/addons/binding/org.openhab.binding.elerotransmitterstick/src/main/java/org/openhab/binding/elerotransmitterstick/handler/EleroGroupHandler.java)

The workaround is also present in original commit
`89af33d5b4e2ef7b5037797082b310962597edff`. That commit has author date
2017-02-07 and committer date 2017-09-01. The author date is not evidence that
the code was publicly available in February.
[Original transport worker](https://github.com/openhab/openhab-addons/blob/89af33d5b4e2ef7b5037797082b310962597edff/addons/binding/org.openhab.binding.elerotransmitterstick/src/main/java/org/openhab/binding/elerotransmitterstick/stick/TransmitterStick.java)

The exact 2020 fix is commit `ee72664d475069d8394c09746e86e2a86e5f07e3`,
relative to parent `220115c7342b589ff2386111c363f7f8e1a3d04a`:

1. `SerialConnection` records the last transmitted packet.
2. A CHECK confirmation is accepted only for a CHECK request.
3. `Response.isResponseFor()` decodes the channel mask in the last request
   and requires an exact match with the reply's channels.
4. The code no longer copies a previous stored response into a local variable
   and returns it immediately after sending a new request. Each new request
   starts with a null response and waits for the current operation.
5. The checksum call now receives the input bytes instead of the newly allocated
   output array. The old extra byte was initially zero, so this is not a new
   checksum algorithm.

These are client changes. They do not install new stick firmware.
[Exact patch](https://github.com/openhab/openhab-addons/commit/ee72664d475069d8394c09746e86e2a86e5f07e3)

An offline Java experiment compiled the actual, unmodified `Response`,
`ResponseUtil`, `ResponseStatus`, and `CommandPacket` classes at the pinned
Bier revision. Only the `HexUtils.bytesToHex` display helper was substituted.
The guard rejected all 29 complete Easy Control failure replies assigned to
cases in the first capture. A synthetic matching-channel reply was accepted;
wrong-channel and zero-channel controls were rejected. The separate interruption
reply was not assigned to a single fixture request and is not in those 29 cases.
This experiment does not run the full openHAB binding or its serial threads.

The guard matches channels, not request identity: the same synthetic channel-1
reply matches both channel-1 UP and channel-1 DOWN. Thus it cannot distinguish
an old same-channel reply from a reply to a newer command. The wire response
has no transaction ID or echoed key byte. Correct sequencing remains necessary.
The source also removes an ordinary command from the due set after
`sendPacket()` returns, even when that return value is null; this path does
not retry an unacknowledged movement command.

The actual status-mapping code maps INTERMEDIATE to 25 and VENTILATION to 75;
most other non-endpoint states map to 50, including movement and fault states.
These numbers are host presentation choices, not measured travel percentages.

- [Replay output](usb-stick-2026-11-04/bier-replay/replay.jsonl)
- [Source hashes and experiment scope](usb-stick-2026-11-04/bier-replay/manifest.json)
- [Historical commit references](usb-stick-2026-11-04/bier-replay/history.json)
- [Offline reproduction script](usb-stick-2026-11-04/bier-replay/reproduce.py)

### Follow-up coverage and isolation controls

The follow-up adds INFO masks `0006`, `0007`, `0101`, `0102`, `0103`,
`4001`, `4002`, and `4003` to both interfaces. Masks with only one of the
two paired channels selected give the usual zero-channel Easy Control reply.
Masks with both paired channels selected give no Easy Control reply in the
4.25-second window, regardless of the tested unpaired bits. Thus the observed
failure pattern follows the selected paired channels in these tests, rather
than simply the number of bits set in the mask.

The follow-up also sends experimental native keys `21` and `41` with the same
native parameter `01` as the ordinary UP/DOWN button path, on masks `0001`,
`0002`, and `0003`. They produce NACKs with no reachable receiver. This does
not prove that the firmware supports these keys, that it sends those exact RF
bytes, or that a motor would treat them as full travel.

The longer native group capture is the material correction to the first matrix:

| Capture | Channel-1 NACK after request | Channel-2 NACK after request | Observation window |
| --- | --- | --- | --- |
| Follow-up group INFO `0003` | 3.510 seconds | 6.972 seconds | 8.25 seconds |
| Clean isolation group INFO `0003` | First chunk 3.412 seconds; complete frame 3.462 seconds | 6.927 seconds | 14.25 seconds |

The extended interface therefore reports failure for both paired channels,
in sequence. The first 4.25-second windows saw only the first failure reply;
they did not establish that only channel 1 was processed. Easy Control group
INFO gives no reply even in the 8.25- and 14.25-second windows. This is a USB
failure-path result. It does not show which RF packets were transmitted or
whether successful group movement would be simultaneous.

The chained follow-up captured a channel-2 NACK after a zero-mask request and
another after a bad-checksum request. These observations alone did not show
that either request was accepted. The isolation run starts with an 8.25-second
passive capture with no received bytes. It then tests valid single-channel INFO
requests, bad-checksum native INFO in both channel directions, zero-mask native
INFO after each valid channel, and bad-checksum Easy INFO. In this clean sequence:

- Each valid single-channel INFO gives the expected failure reply.
- Neither bad-checksum native INFO request gives a reply within 4.25 seconds.
  An additional 4.25-second passive window after the first gives no bytes.
- Neither zero-mask native INFO gives a reply within 4.25 seconds.
- The bad-checksum Easy INFO gives no reply within 4.25 seconds.
- The following CHECK succeeds and version/pairing data match the baseline.

Inference: the NACKs observed after invalid requests in the chained matrix
are consistent with late results from preceding group operations, rather than
evidence that invalid requests were accepted. The exact internal scheduling
cannot be derived from the USB capture alone. An invalid or zero-mask request
must not be used as a barrier that is assumed to cancel an earlier operation.

A final isolated interruption test sends native INFO for channel 1, then native
INFO for channel 2 after 155 ms. Only the channel-2 NACK arrives, 3.460 seconds
after the second request, with no further reply in the 14.25-second window.
This supports replacement of the earlier single-channel operation in this
failure path. It does not prove how the firmware schedules every group case.

Across all five live sessions there are **172 serial requests and 129 received
frames**. Every received frame has a valid length and checksum. The 171 test
records include four passive capture windows; the first session also records
interruption requests and final STOP separately from its named cases. Five
requests intentionally have bad checksums. Every captured version and channel
discovery reply matches the initial firmware `01.10` and paired channels 1/2.
All serial processes exited and released the port after the final capture.

- [Follow-up traffic](usb-stick-2026-11-04/followup-session.jsonl) and [probe snapshot](usb-stick-2026-11-04/followup_matrix.py)
- [Clean isolation traffic](usb-stick-2026-11-04/isolation-session.jsonl) and [probe snapshot](usb-stick-2026-11-04/isolation_matrix.py)
- [Isolated interruption traffic](usb-stick-2026-11-04/interruption-isolated.jsonl) and [probe snapshot](usb-stick-2026-11-04/interruption_isolated.py)
- [Updated counts, controls, and limits](usb-stick-2026-11-04/summary.json)

### Work that requires other equipment or an external source

| Remaining question | Required evidence |
| --- | --- |
| Does `21`/`41` cause full travel on the reported Raffstore variant? | The identified powered receiver in range, with observed movement and captured RF packets. A NACK without that receiver cannot answer this. |
| Does the native interface emit different RF data from Easy Control? | A separate compatible RF receiver or logic-analyser capture of the stick's CC1101 SPI link, with matched serial timestamps. |
| Does group control work when receivers can reply? | At least two identified, paired, reachable receivers; record each receiver's response and movement. The current failure path cannot establish successful group semantics. |
| Does USB switch-state numbering translate the RF light-state values? | A paired switching receiver or light and matched USB/RF status captures. |
| What does the stick firmware do internally? | An actual firmware image or verified hardware read access. No firmware image or flash-read operation was found in the inspected Elero package or Bier source paths. Read protection has not been measured. |

No firmware was extracted or disassembled in these sessions. No new RF command
meaning, receiver compatibility, or firmware version compatibility is inferred
from elapsed time, missing receivers, or generic failure replies. The initial
research did not change component code or issue disposition; the later
source-history-based correction is documented below.

### Consequences for issue #63

The official USB document and Bier's binding support the five ordinary USB
key names. Neither source documents `21` as full UP or `41` as full DOWN.
Those meanings remain receiver-specific community RF observations. Likewise,
`44` is officially an intermediate-position command; it must not be described
as universally full DOWN plus tilt.

The USB status table assigns `10` to switched OFF and `11` to switched ON.
Before the later correction below, the project's RF constants used `0F`/`10`
for light OFF/ON. The USB audit alone does not establish whether the stick
translates those values. The source-history review below provides additional
evidence for the correction; paired USB/RF validation remains unavailable.

The unavailable motors leave the movement and motor-variant parts of #63 open.
The next acceptance test needs an identified receiver and a separate RF capture:
record the serial request, original RF bytes, decoded RF command, receiver reply,
and observed movement for each short/long UP/DOWN and preset operation.

### Light-state correction after source-history review

The owner has no physical light receivers available and authorized a fix if the
source evidence supports it. Further history review found a specific incomplete
off-by-one correction, rather than independent confirmation of the old values:

- Original commit `60c98c6a1cfe90624238430c6cd71950eead5832` defines a consecutive
  status list after MOVING_DOWN: STOPPED `0C`, TOP_TILT `0D`, BOTTOM_TILT `0E`,
  OFF `0F`, ON `10`. The eleropy list also omits the unused `0C` entry.
- [Commit d8e2166](https://github.com/andyboeh/esphome-elero/commit/d8e216647ab0931fa7e793dbdca9b5a3202bf816)
  corrects STOPPED to `0D`, TOP_TILT to `0E`, and BOTTOM_TILT to `0F`, but leaves
  OFF/ON unchanged. That creates the BOTTOM_TILT/OFF collision inherited here.
- Elero's official status table includes the gap at `0C` and explicitly assigns
  switching receiver OFF/ON to `10`/`11`. Bier's independently written USB parser
  uses the same complete table. Other ESPHome forks sharing the original
  constants are not independent evidence for the old mapping.

Decision: correct LIGHT_OFF to `10` and LIGHT_ON to `11`, retaining cover endpoint
`0F`. This is a source-based correction with high confidence in an inherited
off-by-one error; it is not a newly captured RF observation. Physical light
validation and USB-to-RF translation remain untested.

The old frontend also used `0F` to infer a light receiver, so a cover endpoint
report could change its displayed type to light. The fix uses only `10` and `11`
for that inference. Backend labels, schema, generated types, mock data, protocol
documentation, and bundled frontend are updated together.

Regression tests use literal bytes rather than the constants under test. Before
the fix, all three new C++ behaviour tests and all three frontend tests failed.
After the fix, all 512 C++ unit tests and the three frontend tests pass; frontend
type checking and production build also pass. Cover state values are unchanged.
