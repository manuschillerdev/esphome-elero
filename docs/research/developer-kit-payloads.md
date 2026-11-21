# Elero Transmitter Stick developer kit vs. hub payloads

The hub defines **all five send keys and all 17 status values in the public Easy Control PDF**. The bundled Windows client, `Sombrero.exe`, contains a second, more detailed message builder. It represents the pressed buttons and the gesture in a single byte. It also has separate flags and five extra status labels. These details give us specific fields to look for in remote recordings.

**Scope.** The kit describes the connection from a computer to a USB stick. The hub sends radio packets directly. A matching numeric value establishes a radio mapping only when the message type, field and observed receiver effect also agree. The English and German download archives had the same SHA-256 hash on 2026-11-21. The other downloads on this page are KNX/SMI actuator data files. [Elero developer downloads](https://www.elero.com/en/downloads-service/apps-and-programs/other-programs) · [Hub constants](../../components/elero/elero_packet.h)

## The public Easy Control interface

| Field | Elero value and meaning | Hub comparison |
|---|---|---|
| Send key | `0x10` stop, `0x20` up, `0x40` down | Match `command::STOP`, `UP`, `DOWN`. |
| Send key | `0x24` tilt or ventilation, `0x44` intermediate | Match `command::TILT` and `INTERMEDIATE`. A VarioCom owner also identified these two values from radio logs. [Double-tap observations](https://github.com/andyboeh/esphome-elero/issues/4#issuecomment-2128590751) |
| Status | `0x00` no information | `state::UNKNOWN`. The hub also uses command `0x00` for a targeted status request or a channel release in other contexts. |
| Status | `0x01` top, `0x02` bottom, `0x03` intermediate, `0x04` tilt/ventilation | Match `TOP`, `BOTTOM`, `INTERMEDIATE`, `TILT`. These report where movement stopped. |
| Status | `0x05` blocked, `0x06` overheated, `0x07` timeout | Match `BLOCKING`, `OVERHEATED`, `TIMEOUT` numerically. The client describes `0x07` as a stop at the end of run time. The PDF says only “Timeout”. A lost radio reply is a separate event. |
| Status | `0x08`, `0x09` starting up/down. `0x0A`, `0x0B` moving up/down | Match `START_MOVING_UP/DOWN` and `MOVING_UP/DOWN`. |
| Status | `0x0D` stopped at an unspecified position | `STOPPED`. The PDF skips `0x0C`. The client gives it a meaning below. |
| Status | `0x0E` top end position coincides with tilt/ventilation. `0x0F` bottom end position coincides with intermediate | The project names are `TOP_TILT` and `BOTTOM_TILT`. In particular, `0x0F` does not establish a slat angle. The project's cover-status decoder assigns a tilt value to both. [Cover status decoder](../../components/elero/elero_packet.cpp) |
| Status | `0x10` switching device off, `0x11` switching device on | Match `LIGHT_OFF`, `LIGHT_ON`. Elero uses these for a switching device generally. |

The USB frame has its own operation codes. `0x4A/0x4B` request and return the learned-channel mask. `0x4C` sends a key, `0x4D` returns status for a channel, and `0x4E` requests status. In particular, USB `Easy_Check` discovers learned channels. USB `Easy_Info` is the closer functional match for the hub's radio CHECK. USB masks address channels 1–15. The hub supports radio channel selectors and receiver addresses. The USB framing, checksum and ACK waiting times do not define radio fields or radio timing.

## What the sample program adds

The client builds the key byte as **button mask OR gesture value**. A mask uses one bit per button. The low four bits select a gesture. This comes from the control order in `FormIndividuell.InitializeComponent` and the assignments in `FormIndividuell.BuildTelegrammString`. The send handler copies the resulting bytes into a USB request.

| Part of the key byte | Values assigned by the client |
|---|---|
| Button mask | `0x00` released, `0x10` STOP, `0x20` UP, `0x40` DOWN, `0x80` P |
| Combined buttons offered | `0x30` UP + STOP, `0x50` DOWN + STOP, `0x60` UP + DOWN, `0x70` UP + DOWN + STOP, `0x90` P + STOP, `0xA0` P + UP, `0xC0` P + DOWN, `0xE0` P + UP + DOWN, `0xB0` P + STOP + UP, `0xF0` all four |
| Gesture, no double tap | `0x00` default, `0x01` 3 s, `0x02` 6 s, `0x03` 9 s |
| Gesture, double tap | `0x04` double tap, `0x05` double tap + 3 s, `0x06` double tap + 6 s, `0x07` double tap + 9 s |
| Other gesture selection | `0x08` Auto. The client also has a separate Auto/Manual flag in the next byte. |

| Example | Kit result | Evidence for the hub |
|---|---|---|
| Double UP / DOWN | `0x24` / `0x44` | Matches both named hub constants and the logged VarioCom gestures. In Easy mode, clicking one preset button in the sample calls `SendDrive` once with the corresponding key. The stick handles the radio exchange. |
| Held UP / DOWN | `0x21` / `0x41` with the 3 s selection | Matches `FULL_UP` / `FULL_DOWN` numerically. The hub uses a targeted radio packet for these. The client's label does not set a universal physical hold threshold. [Radio evidence](../TESTING_STEP_RECEIVERS.md#rf-capture-evidence-and-limits) |
| Double UP / DOWN + 3 s | `0x25` / `0x45` | Both are definite **client encodings**. Their use for Pulse Plus working positions still needs a labelled radio recording. One JA Pulse report proposed them for presets. The author later corrected the tilt setting to `0x24`. [Report](https://github.com/andyboeh/esphome-elero/issues/1#issuecomment-1956567857) · [Correction](https://github.com/andyboeh/esphome-elero/issues/1#issuecomment-1958821842) |
| Clear a stored position | Client can express DOWN + STOP + 3 s as `0x51`, or UP + STOP + 3 s as `0x31` | Elero documents these physical holds. Compare a labelled remote trace with the composite byte, preceding press, following release and any flags. Receiver memory after the operation is the deciding test. No mapping for the separate **save** gesture follows just from these values. [Programming guide p. 2](../manuals/en/transmitter-programming.pdf#page=2) |
| Programming P | Base mask `0x80` | Matches the hub's canonical single-channel P byte. The hub has observed `0x81` and `0x84` in different pairing packet formats. Their numeric overlap with P + gesture values does not identify the originating gesture. [Pairing captures](../../.pi/skills/elero-protocol/SKILL.md#observed-p--learn-in-rf-envelopes) |

## Fields beyond the key byte

The client's individual-message form builds seven bytes. The USB send handler adds a separate request-for-acknowledgement byte. Positions in this table start at zero **in the client buffer**. The decrypted radio block has a different layout. Its first two bytes are used by the radio encoding.

| Client byte | Meaning established by the assignments | Comparison with the hub |
|---|---|---|
| 0 | Message selection: `00` byte send, `02` bit info, `10` bit empty (hex). | The radio builders write two prefix bytes outside the encrypted block. Direct correspondence requires a capture. |
| 1 | Device type. The client assigns `00` SoloTel, `01` MonoTel, `02` LumeroTel, `03` VarioTel, `04` TempoTel, `05` MarkiLuxVarioTel, `06` Invio and `07` Scene. It also lists sensor types `20–28` (hex). | The hub uses `00 03` for a single button and defaults to `00 04` for targeted transmission. The client gives a plausible reason for those prefixes. A prefix alone cannot identify a physical model. |
| 2 | Button mask and gesture. | Strong correspondence for the documented movement and preset actions with radio `payload_offset::COMMAND`. |
| 3 | Independent flags: `01` Key Man Auto, `02` shading, `04` key status, `08` delete destination, `10` priority, `20` timer off, `40` sensor move, `80` value overflow (hex). | The hub writes zero to `COMMAND2` in its normal transmit builders. These are candidates for the mode, scheduling and deletion gaps. The flag labels alone do not specify which combination reads, sets or clears receiver state. |
| 4 | Run time in seconds, or a Lumo command according to the form's selection. | Hub builders write zero to decrypted byte 4. Hub travel-time estimates run locally. |
| 5 | Engage motion checkbox, encoded as 0 or 1 in the individual form. | Hub builders write zero to decrypted byte 5. The built-in preset and movement buttons use additional values in this position in extended mode. |
| 6 | Info value. | The hub reads decrypted byte 6 as state on radio status packets and writes zero on normal command transmission. |

For a useful cross-check, `Form1.SendDrive` sends just the key in USB Easy mode. In extended mode its eight data bytes are `00 04 key flags 00 motion 00 01`. The built-in buttons supply motion values `01` for UP/DOWN, `00` for STOP and tilt, and `04` for intermediate. Thus even two correct key bytes can have different accompanying fields. A separate form builds sensor messages with readings and thresholds. A value in that format does not establish a new handheld movement command. [Radio builders](../../components/elero/elero_packet.cpp) · [Radio block layout](../PACKET_STRUCTURE.md)

## Five extra status values

Both the Easy and extended reply handlers call `Form1.DecryptInfoByte`. Its switch includes the following values in addition to the 17 in the PDF. None has a named `packet::state` constant at revision `6abd2c4`.

| Value | Meaning shown by the client |
|---|---|
| `0x0C` | “Delete in Remote” |
| `0x12` | Scene set |
| `0x13` | Scene started |
| `0x14` | Scene stopped |
| `0x15` | Scene error |

These are receive-side labels. For example, seeing `0x0C` in the client's status decoder does not justify sending `0x0C` to delete a remote. Before assigning radio state names, check for these values in the **state field** of receiver replies. Keep channel, packet type and user action with the recording.

## Verification

Compared the PDF's `Key_Data` and `Info_Data` rows with the numeric sets in the hub header. Inspected the .NET instructions in the shipped client without running it. The useful methods are `FormIndividuell.BuildTelegrammString`, `FormIndividuell.bSendIndividual_Click`, `Form1.SendDrive`, `Form1.SendInfo` and `Form1.DecryptInfoByte`. For button labels and index order, use `FormIndividuell.InitializeComponent`. No radio transmission or receiver test was part of this comparison.

| File | SHA-256 |
|---|---|
| Developer ZIP (Elero download 1432) | `080af0a41829623decd72d8f6f469dd6bf1c704ab934b0a1a72f716c06824dc9` |
| `Easy Control Transmitter Stick.pdf` | `1d63724b9be3bdd503134bf675b3db5bc8d76a6c97d3deb1f975056f2e50f97d` |
| `Sombrero.exe` | `971ef664da50fb7411d3882e6415802dc533e73c0e2cf86648272405927954f1` |
