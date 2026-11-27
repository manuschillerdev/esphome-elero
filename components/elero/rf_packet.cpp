#include "elero.h"
#include "esphome/core/log.h"

namespace esphome::elero {

optional<RfPacketInfo> RfPacketInfo::decode(const uint8_t *buf, size_t buf_len) {
  using namespace packet;

  const ParseResult r = parse_packet(buf, buf_len);
  if (!r.valid && !r.complete_frame) {
    if (r.reject_reason) {
      ESP_LOGV("elero", "Packet rejected: %s", r.reject_reason);
    }
    return {};
  }

  RfPacketInfo pkt{};
  pkt.src = r.src_addr;
  pkt.dst = r.dst_addr;
  pkt.raw_only = !r.valid;
  pkt.destination_bytes = r.valid ? r.dests_len : 0;
  if (r.valid && uses_three_byte_address_envelope(r.type)) {
    pkt.num_address_dests = r.num_dests;
    memcpy(pkt.dst_addrs, r.dst_addrs, sizeof(pkt.dst_addrs));
  }
  pkt.channel = r.channel;
  pkt.type = r.type;
  pkt.type2 = r.type2;
  pkt.command = r.command;
  pkt.state = r.state;
  pkt.cnt = r.counter;
  pkt.rssi = r.rssi;
  pkt.lqi = r.lqi;
  pkt.crc_ok = (r.crc_ok != 0);
  pkt.hop = r.hop;
  memcpy(pkt.payload, r.payload, sizeof(pkt.payload));
  pkt.trailing_length = r.trailing.length;

  // Copy raw bytes for WebSocket broadcast
  size_t raw_total = static_cast<size_t>(r.length) + PACKET_TOTAL_OVERHEAD;
  pkt.raw_len = (raw_total <= CC1101_FIFO_LENGTH) ? static_cast<uint8_t>(raw_total) : CC1101_FIFO_LENGTH;
  memcpy(pkt.raw, buf, pkt.raw_len);

  return pkt;
}

}  // namespace esphome::elero
