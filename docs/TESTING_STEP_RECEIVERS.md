# Test step receivers and continuous travel

Feature branch: `feat/step-receiver-controls`.

## Install the branch

In the existing ESPHome configuration, select the feature branch:

```yaml
external_components:
  - source: github://manuschillerdev/esphome-elero@feat/step-receiver-controls
    refresh: 0s
```

Keep the existing board, radio, and adapter configuration. Compile and install
that configuration using the normal ESPHome workflow. The bundled web UI is
included in the branch. Reload `/elero` after the firmware update.

## Test one cover

1. Open **Manage** and expand a saved cover. Save a newly discovered cover first.
2. Start with **Movement mode: Standard**. The test buttons are independent of
   the selected mode and UP/DOWN command overrides.
3. Test **Full up**, **Stop**, **Full down**, and **Stop**. Full travel targets the
   configured receiver address. Confirm which blind moves and whether travel
   continues without holding the button.
4. From a stopped position, test **Short press up** and **Short press down**. Each action sends
   one short press/release cycle. Record whether it turns the slats, makes a
   short movement, or starts continuous travel. Channel peers can also respond.
5. If full travel works, select **Step receiver**, then save the cover. Normal
   Open/Close now use full travel, including Home Assistant commands. Follow the
   existing reboot prompt in native API mode if one is shown.
6. Test **Intermediate** and, if tilt is enabled, **Saved tilt** separately. These
   recall receiver presets. They do not mean full closure or a chosen slat angle.
7. Reboot and check the movement mode. Export a backup and confirm the cover has
   `movement_mode: "step"`. Restore tests should retain that mode.
8. To return to the old movement selection, save **Standard**. The stored custom
   UP/DOWN values are retained. Explicit `0x21`/`0x41` overrides now select targeted
   packets even in standard mode; remove those overrides to return to `0x20`/`0x40`.

## Home Assistant buttons

Each enabled cover exposes **Short press up**, **Short press down**, **Full up**,
and **Full down** in both native API and MQTT modes. These are stateless buttons;
normal cover Open/Close/Stop controls remain available. In MQTT mode they use the
existing cover device and command topic. In native mode they are registered at
boot, so new covers require the normal native-mode reboot.

Verify that the buttons appear on the intended cover in Home Assistant. A short
press on a standard receiver can start full travel, just as on its physical
remote. Full travel is independent of the selected movement mode. Rename should
preserve MQTT entity identities; disabling or removing the cover should remove
its MQTT buttons. Native buttons must not send commands for a disabled or removed
cover, even before the required reboot updates the entity list.

## Home Assistant directional tilt controls

Enable **Tilt**, select **HA tilt control → Directional steps**, and Save to NVS.
Use a receiver whose short UP/DOWN presses produce the required slat movement.
The setting is independent of the Open/Close movement mode. Apply the normal
native-mode reboot when changing exposed capabilities.

In each HA transport, test tilt-open and tilt-close repeatedly. Expected RF is
the same short press/release sequence as the separate Short press buttons.
The values below 50% select DOWN; 50% or above select UP. A value such as 75%
does not request a 75% angle. Check that custom full-travel bytes do not change
these short presses, and that the Saved tilt shortcut still recalls its preset.

Repeat with Saved preset selected: both HA tilt directions must recall the
configured saved preset. Export and restore a backup to check the selected mode;
an old backup without `tilt_mode` must use Saved preset. Record the radio chip
(CC1101, SX1262, or SX1276) and receiver model for each hardware test.

## Record the results

Record the receiver/motor and remote models, source address, receiver address,
logical channel, selected mode, and exact button pressed. Keep RF logs with
packet type, command, `type2`, `hop`, timestamps, and receiver status replies.
Note whether CHECK polling changes movement or an old endpoint/STOPPED status
arrives immediately after a command. These are known community reports, not
behaviour that this branch assumes or filters out.

Expected full-travel packets: type `0x6a`, command `0x21` or `0x41`,
`type2=0x10`, `hop=0x00`, one 24-bit receiver destination. Expected steps:
channel-addressed type `0x44`, command `0x20` or `0x40`, then `0x00` release.
A command byte and packet type can both be `0x44`; they describe different fields.

## Official remote manual evidence

The physical short-press feature is documented by Elero. Its result depends on
the receiver and operating mode. The following are manufacturer instructions
for physical hand-held remotes, not inferred software command tables.

| Official manual | Movement instructions | Feature evidence |
|---|---|---|
| [MonoTel 2](https://www.elero.com/en/downloads-service/downloads?tx_avelero_downloads%5Baction%5D=download&tx_avelero_downloads%5Bcontroller%5D=Download&tx_avelero_downloads%5Bdownload%5D=320&cHash=bcb8ff9e75585c395c63a5016eff85e6) | PDF 7; printed EN 13 | Short UP/DOWN is jog/pulse movement on the described Venetian receivers; a longer press starts endpoint travel. A brief press can start full travel on roller shutters/awnings. |
| [VarioTel 2](https://www.elero.com/en/downloads-service/downloads?tx_avelero_downloads%5Baction%5D=download&tx_avelero_downloads%5Bcontroller%5D=Download&tx_avelero_downloads%5Bdownload%5D=325&cHash=78a602382d851e9c1b95f20927d7eccb) | PDF 8; printed EN 14 | Short UP/DOWN is jog/pulse movement on the described Venetian receivers; a longer press starts endpoint travel. A brief press can start full travel on roller shutters/awnings. |
| [TempoTel 2](https://www.elero.com/en/downloads-service/downloads?tx_avelero_downloads%5Baction%5D=download&tx_avelero_downloads%5Bcontroller%5D=Download&tx_avelero_downloads%5Bdownload%5D=324&cHash=23c1a06c0aba16153d980c7601f322c9) | PDF 29; printed EN 29 | Short UP/DOWN is jog/pulse movement on the described Venetian receivers; a longer press starts endpoint travel. A brief press can start full travel on roller shutters/awnings. |

Basic movement requires a learned transmitter/channel and set drive end
positions. It does not require a saved intermediate or tilt position. Those
presets have separate recall instructions. On the described Venetian receivers,
UP/DOWN held until the status indicator lights requests endpoint travel. The
manuals do not give a numerical short/long threshold for these instructions.

Combio JA Pulse has a configurable pulse duration stored in the receiver. See
MonoTel 2 PDF pages 8–9 / printed EN 15–16; VarioTel 2 PDF page 9 / printed
EN 16–17; and TempoTel 2 PDF pages 31–32 / printed EN 31–32. This setting affects
the receiver's jog/pulse operation. It is not a saved height/tilt preset and is
not the delay between our RF press and release packets.

Therefore the UI and HA entities use **Short press**, not a promise of a fixed
step distance. A normal roller shutter can move fully after this action, as it
does after a brief press on its physical remote.

The [source manifest](research/short-press-manual-evidence-2026-11-07.json) records
manufacturer URLs, document revisions, exact page locations, and SHA-256 hashes
of the PDFs checked on 2026-11-07. It contains metadata only; the full manuals
are not bundled with the component.

## RF capture evidence and limits

- [VarioCom remote capture](https://github.com/andyboeh/esphome-elero/issues/4#issuecomment-2129977456)
  shows channel DOWN followed by receiver-addressed long DOWN.
- [Eleropy hardware report](https://github.com/stanleypa/eleropy/issues/5#issuecomment-1907562655)
  confirms targeted `0x21`/`0x41` operation with normal and step receivers.

The implementation sends the targeted full-travel command directly, as in the
successful eleropy test. It does not first send a short press and wait two seconds.
It adds no measured tilt angle, new pairing procedure, or validated receiver
profile beyond these explicit actions. Hardware results are still required.

The official remote manuals establish the user-visible short-press/jog/pulse and
endpoint-travel functions. They do not specify RF command bytes, packet types,
repeat counts, release byte, or timing. The branch's three press packets followed
by three release packets, at the existing 10 ms repeat interval, are an
implementation choice supported by the recorded protocol context; they are not
manufacturer-certified replay of every remote. Feature evidence and exact RF
parity must remain separate in hardware acceptance results.
