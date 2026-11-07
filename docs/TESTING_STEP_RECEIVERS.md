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
4. From a stopped position, test **Step up** and **Step down**. Each action sends
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

## Evidence and limits

- [VarioCom remote capture](https://github.com/andyboeh/esphome-elero/issues/4#issuecomment-2129977456)
  shows channel DOWN followed by receiver-addressed long DOWN.
- [Eleropy hardware report](https://github.com/stanleypa/eleropy/issues/5#issuecomment-1907562655)
  confirms targeted `0x21`/`0x41` operation with normal and step receivers.

The implementation sends the targeted full-travel command directly, as in the
successful eleropy test. It does not first send a short press and wait two seconds.
It adds no measured tilt angle, new pairing procedure, or validated receiver
profile beyond these explicit actions. Hardware results are still required.
