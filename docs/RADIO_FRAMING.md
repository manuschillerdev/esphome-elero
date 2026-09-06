# Radio framing and TX/RX ownership

`elero_packet` builds and parses Elero envelopes; `Elero` delivers decoded frames. Radio drivers own SPI, interrupt
acknowledgment, receive framing, integrity checks, and return to RX. A driver
returns only complete frames. The current parser boundary retains the legacy
`[length | body | RSSI | LQI/CRC]` representation; it is an adapter format, not
necessarily the bytes read from the chip.

SX1262 and SX1276 share `SemtechTxFsm`. Its phases are Prepare, WaitTxDone,
ReturnToRx, WaitRxReady, and Recover. Chip-specific hooks implement those phases.
CC1101 retains its separate FSM because it also proves TX started. TX success
requires completion and successful return to reception.

## Receive behavior

- CC1101 consumes a length byte only when another byte is already buffered, and
  leaves the body in the FIFO until all data and status bytes are available.
  It consumes exactly one frame. A partial frame owns RX for at most 20ms.
  Hardware CRC auto-flush is disabled because it could discard a body after its
  length was consumed. The driver rejects bad CRCs using the hardware status.
- Semtech captures use software PN9 and CRC-16. The hardware capture capacity is
  64 bytes; this is not a restriction that transmitters must send 64 bytes.
  Sync detection starts an 8ms capture window (64 bytes at 76.8 kbaud take about
  6.67ms). The driver freezes reception, reads the capture, validates the CRC at
  the length-indicated offset, and rearms RX. Hardware DONE can finish a full
  capture earlier. SX1262 uses single RX and clears RAM before each capture;
  SX1276 disables auto-restart and stops reading at FifoEmpty.
- RSSI is captured at sync rather than sampling background noise after a short
  transmission ends. LQI is unavailable on Semtech; its value remains zero.
  CRC_OK is set only after software CRC verification.
- The RF task services RX before dequeuing TX or frequency changes and defers
  those requests while `receiving()` is true. Capture timeouts release that
  ownership, including malformed or truncated frames.

The shared CRC helper validates framing/integrity only. Elero packet types,
address counts, and the implementation's 57-byte body limit are validated by the
protocol parser, not duplicated as a 27..30-byte driver whitelist.

## Evidence and hardware validation

Host tests exercise production SPI-facing driver code for corrupt CRCs, lengths
27..57, partial CC1101 frames, adjacent frames, and short Semtech captures with
no DONE interrupt and no transmitted padding. They also cover SX1262 status
byte order, BUSY failures, initialization, and the shared TX sequencer.

The bounded Semtech capture strategy is new and still needs RF bench validation.
In particular, verify SX1262 partial RX RAM contents survive entry to standby,
SX1276 FIFO contents and sync flags behave as expected with auto-restart off,
and reception rearms before the next real remote repeat. Host mocks do not
establish these hardware behaviors. Test short button/P frames, status frames,
five-or-more-selector groups, truncated/noisy frames, and fast post-TX replies
on both chipsets before deploying this receive change.

The 8ms window trades some minimum inter-frame spacing for receiving variable
length frames with hardware whitening disabled. It cannot capture a second
frame whose sync arrives while the current capture is still owned.

Reference: [Semtech SX1261/2 datasheet, sections 6.2.3 and 13.5](https://files.waveshare.com/wiki/SX1262-XXXM-LoRaWAN-GNSS-HAT/DS_SX1261-2_V1.2.pdf).
`GetStatus` returns RFU during the opcode and Status during the following NOP.
The datasheet describes programmable CRC and LFSR whitening; historical notes
about an intrinsically data-dependent scrambler or universally incompatible
hardware CRC should not be treated as hardware specifications. The software
encoding used here preserves the project's existing over-the-air TX format.

### RX handoff and recovery

The RF task drains rejected frames as well as valid ones. A zero-byte result
can mean either a discarded frame or an incomplete capture; the task rechecks
hardware availability and defers TX/frequency changes while RX work remains,
including when the four-frame drain budget is exhausted.

SX1262 single-RX completion legitimately leaves the chip in standby. The health
check preserves pending sync/RX-done events for draining instead of resetting
the receiver. CC1101 recovery checks RX after reset before declaring permanent
failure. Initialization propagates RX readiness failure, and successful recovery
clears consecutive failure counters. SX1276 initialization and recovery wait for verified mode/PLL readiness
with bounded timeouts; failure to enter standby stops dependent operations.

TX queue entries and completions carry a per-client attempt ID. IDs are created,
invalidated, and checked on Core 1; Core 0 only transports their copied values.
Cancellation invalidates the attempt immediately for both normal commands and
learn-in sessions. Already queued RF transmissions are not retracted by this
mechanism. Driver FSMs own hardware timeouts; clients do not time out queue
waiting or consume retries before an actual failure is reported.

The RF task retains one pending completion until the bounded Core 1 queue accepts
it. While it is pending, new TX admission pauses and RX processing continues.
If the radio fails permanently, the task reports failures for accepted TX work
without further SPI access, rather than stranding clients waiting for completion.

## Supported group size

Selector-group TX supports **2–31 destinations per packet/source address** on
all three chipsets. A group body contains 26 fixed bytes plus one byte per
selector. The existing 57-byte receive-body limit therefore allows 31 selectors:
58 bytes submitted to the driver, 60 bytes on air including software/hardware
CRC. Single-selector commands use the single-channel envelope. Oversized groups
are rejected before RF enqueue; the builder also rejects them before writing.

This is a software support limit, not a measured maximum supported by every
motor. History matters: `07f0a95` derived the former 37-selector TX limit from
the bare CC1101 FIFO, without reserving software CRC; `16e2630` introduced the
former generic 20-destination parser guard before group TX existed. Neither
established a hardware group limit. The shared constant now derives from the
existing receive-body limit, keeping TX and RX compatible on all drivers.

## Transport boundary

`RfTransport::step()` owns one bounded RF iteration: flush retained completions,
drain RX, admit at most one request, advance TX, drain fast replies, and check
idle radio health. Its three hub hooks are nonblocking request/completion queues
and synchronous frame delivery. The hub retains task creation, ISR notification,
watchdog feeding, sleep, timestamps/decoding, and Core 1 client dispatch. Frequency
accessors report the latest accepted configuration request; hardware applies
requests in queue order after TX and unread RX have finished.

SX1262 watchdog and TX recovery use the same checked restore sequence. Any
required command failure or failure to verify RX triggers full initialization;
three consecutive failed full recoveries mark the radio failed. A successful
soft restore or initialization clears that count.

Host transport tests exercise production orchestration with bounded queues and
a fake radio, including partial RX, rejected frames, missed RX notifications,
bursts, completion saturation, stale attempts, failed drivers, and frequency
ordering. SPI-facing driver tests separately verify maximum-size group TX and
recovery faults. These tests do not establish RF timing or burst performance.
