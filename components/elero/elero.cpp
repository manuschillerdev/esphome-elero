#include "elero.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include <cstring>
#include <algorithm>

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif

namespace esphome {
namespace elero {

static const char *TAG = "elero";
static const uint8_t flash_table_encode[] = {0x08, 0x02, 0x0d, 0x01, 0x0f, 0x0e, 0x07, 0x05, 0x09, 0x0c, 0x00, 0x0a, 0x03, 0x04, 0x0b, 0x06};
static const uint8_t flash_table_decode[] = {0x0a, 0x03, 0x01, 0x0c, 0x0d, 0x07, 0x0f, 0x06, 0x00, 0x08, 0x0b, 0x0e, 0x09, 0x02, 0x05, 0x04};

const char *elero_state_to_string(uint8_t state) {
  switch (state) {
    case ELERO_STATE_TOP: return "top";
    case ELERO_STATE_BOTTOM: return "bottom";
    case ELERO_STATE_INTERMEDIATE: return "intermediate";
    case ELERO_STATE_TILT: return "tilt";
    case ELERO_STATE_BLOCKING: return "blocking";
    case ELERO_STATE_OVERHEATED: return "overheated";
    case ELERO_STATE_TIMEOUT: return "timeout";
    case ELERO_STATE_START_MOVING_UP: return "start_moving_up";
    case ELERO_STATE_START_MOVING_DOWN: return "start_moving_down";
    case ELERO_STATE_MOVING_UP: return "moving_up";
    case ELERO_STATE_MOVING_DOWN: return "moving_down";
    case ELERO_STATE_STOPPED: return "stopped";
    case ELERO_STATE_TOP_TILT: return "top_tilt";
    case ELERO_STATE_BOTTOM_TILT: return "bottom_tilt"; // also ELERO_STATE_OFF (0x0f)
    case ELERO_STATE_ON: return "on";
    default: return "unknown";
  }
}

void Elero::loop() {
  // Drain command queues for runtime-adopted blinds
  for (auto &entry : this->runtime_blinds_) {
    auto &rb = entry.second;
    if (!rb.command_queue.empty()) {
      uint8_t cmd_byte = rb.command_queue.front();
      t_elero_command cmd{};
      cmd.counter = rb.cmd_counter;
      cmd.blind_addr = rb.blind_address;
      cmd.remote_addr = rb.remote_address;
      cmd.channel = rb.channel;
      cmd.pck_inf[0] = rb.pck_inf[0];
      cmd.pck_inf[1] = rb.pck_inf[1];
      cmd.hop = rb.hop;
      cmd.payload[0] = rb.payload_1;
      cmd.payload[1] = rb.payload_2;
      cmd.payload[8] = cmd_byte;
      if (this->send_command(&cmd)) {
        rb.command_queue.pop();
        rb.cmd_counter++;
        if (rb.cmd_counter > 255) rb.cmd_counter = 1;
      }
    }
  }

  if(this->received_) {
    ESP_LOGVV(TAG, "loop says \"received\"");
    this->received_ = false;
    uint8_t len = this->read_status(CC1101_RXBYTES);
    if(len & 0x80) { // overflow - FIFO data unreliable
      ESP_LOGV(TAG, "Rx overflow, flushing FIFOs");
      this->flush_and_rx();
      return;
    }
    if(len & 0x7F) { // bytes available
      uint8_t fifo_count;
      if((len & 0x7F) > CC1101_FIFO_LENGTH) {
        ESP_LOGV(TAG, "Received more bytes than FIFO length - wtf?");
        this->read_buf(CC1101_RXFIFO, this->msg_rx_, CC1101_FIFO_LENGTH);
        fifo_count = CC1101_FIFO_LENGTH;
      } else {
        fifo_count = (len & 0x7f);
        this->read_buf(CC1101_RXFIFO, this->msg_rx_, fifo_count);
      }
      // Log raw bytes at VERBOSE level for analysis
      ESP_LOGV(TAG, "RAW RX %d bytes: %s", fifo_count,
               format_hex_pretty(this->msg_rx_, fifo_count).c_str());
      // Capture to ring buffer if dump mode is active
      this->packet_dump_pending_update_ = false;
      if (this->packet_dump_mode_) {
        this->capture_raw_packet_(fifo_count);
        this->packet_dump_pending_update_ = true;
      }
      // Sanity check
      if(this->msg_rx_[0] + 3 <= fifo_count) {
        this->interpret_msg();
      } else if (this->packet_dump_pending_update_) {
        this->mark_last_raw_packet_(false, "short_read");
        this->packet_dump_pending_update_ = false;
      }
    }
  }
}

void IRAM_ATTR Elero::interrupt(Elero *arg) {
  arg->set_received();
}

void IRAM_ATTR Elero::set_received() {
  this->received_ = true;
}

void Elero::dump_config() {
  ESP_LOGCONFIG(TAG, "Elero CC1101:");
  LOG_PIN("  GDO0 Pin: ", this->gdo0_pin_);
  ESP_LOGCONFIG(TAG, "  freq2: 0x%02x, freq1: 0x%02x, freq0: 0x%02x", this->freq2_, this->freq1_, this->freq0_);
  ESP_LOGCONFIG(TAG, "  Registered covers: %d", this->address_to_cover_mapping_.size());
}

void Elero::setup() {
  ESP_LOGI(TAG, "Setting up Elero Component...");
  this->spi_setup();
  this->gdo0_pin_->setup();
  this->gdo0_pin_->attach_interrupt(Elero::interrupt, this, gpio::INTERRUPT_FALLING_EDGE);
  this->reset();
  this->init();
}

void Elero::reinit_frequency(uint8_t freq2, uint8_t freq1, uint8_t freq0) {
  this->received_ = false;
  this->freq2_ = freq2;
  this->freq1_ = freq1;
  this->freq0_ = freq0;
  this->reset();
  this->init();
  ESP_LOGI(TAG, "CC1101 re-initialised: freq2=0x%02x freq1=0x%02x freq0=0x%02x", freq2, freq1, freq0);
}

void Elero::flush_and_rx() {
  ESP_LOGVV(TAG, "flush_and_rx");
  this->write_cmd(CC1101_SIDLE);
  this->wait_idle();
  this->write_cmd(CC1101_SFRX);
  this->write_cmd(CC1101_SFTX);
  this->write_cmd(CC1101_SRX);
  this->received_ = false;
}

void Elero::reset() {
  // We don't do a hardware reset as we can't read
  // the MISO pin directly. Rely on software-reset only.

  this->enable();
  this->write_byte(CC1101_SRES);
  delay_microseconds_safe(50);
  this->write_byte(CC1101_SIDLE);
  delay_microseconds_safe(50);
  this->disable();
}

void Elero::init() {
  uint8_t patable_data[] = {0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0};

  this->write_reg(CC1101_FSCTRL1, 0x08);
  this->write_reg(CC1101_FSCTRL0, 0x00);
  this->write_reg(CC1101_FREQ2, this->freq2_);
  this->write_reg(CC1101_FREQ1, this->freq1_);
  this->write_reg(CC1101_FREQ0, this->freq0_);
  this->write_reg(CC1101_MDMCFG4, 0x7B);
  this->write_reg(CC1101_MDMCFG3, 0x83);
  this->write_reg(CC1101_MDMCFG2, 0x13);
  this->write_reg(CC1101_MDMCFG1, 0x52);
  this->write_reg(CC1101_MDMCFG0, 0xF8);
  this->write_reg(CC1101_CHANNR, 0x00);
  this->write_reg(CC1101_DEVIATN, 0x43);
  this->write_reg(CC1101_FREND1, 0xB6);
  this->write_reg(CC1101_FREND0, 0x10);
  this->write_reg(CC1101_MCSM0, 0x18);
  this->write_reg(CC1101_MCSM1, 0x3F);
  this->write_reg(CC1101_FOCCFG, 0x1D);
  this->write_reg(CC1101_BSCFG, 0x1F);
  this->write_reg(CC1101_AGCCTRL2, 0xC7);
  this->write_reg(CC1101_AGCCTRL1, 0x00);
  this->write_reg(CC1101_AGCCTRL0, 0xB2);
  this->write_reg(CC1101_FSCAL3, 0xEA);
  this->write_reg(CC1101_FSCAL2, 0x2A);
  this->write_reg(CC1101_FSCAL1, 0x00);
  this->write_reg(CC1101_FSCAL0, 0x1F);
  this->write_reg(CC1101_FSTEST, 0x59);
  this->write_reg(CC1101_TEST2, 0x81);
  this->write_reg(CC1101_TEST1, 0x35);
  this->write_reg(CC1101_TEST0, 0x09);
  this->write_reg(CC1101_IOCFG0, 0x06);
  this->write_reg(CC1101_PKTCTRL1, 0x8C);
  this->write_reg(CC1101_PKTCTRL0, 0x45);
  this->write_reg(CC1101_ADDR, 0x00);
  this->write_reg(CC1101_PKTLEN, 0x3C);
  this->write_reg(CC1101_SYNC1, 0xD3);
  this->write_reg(CC1101_SYNC0, 0x91);
  this->write_burst(CC1101_PATABLE, patable_data, 8);

  this->write_cmd(CC1101_SRX);
  this->wait_rx();
}

void Elero::write_reg(uint8_t addr, uint8_t data) {
  this->enable();
  this->write_byte(addr);
  this->write_byte(data);
  this->disable();
  delay_microseconds_safe(15);
  // TODO: Add error handling - verify SPI transaction success, handle timeout/CRC errors
}

void Elero::write_burst(uint8_t addr, uint8_t *data, uint8_t len) {
  this->enable();
  this->write_byte(addr | CC1101_WRITE_BURST);
  for(int i=0; i<len; i++)
    this->write_byte(data[i]);
  this->disable();
  delay_microseconds_safe(15);
  // TODO: Add error handling - verify all bytes written, handle partial writes
}

void Elero::write_cmd(uint8_t cmd) {
  this->enable();
  this->write_byte(cmd);
  this->disable();
  delay_microseconds_safe(15);
  // TODO: Add error handling - verify command accepted by CC1101
}

bool Elero::wait_rx() {
  ESP_LOGVV(TAG, "wait_rx");
  uint8_t timeout = 200;
  while ((this->read_status(CC1101_MARCSTATE) != CC1101_MARCSTATE_RX) && (--timeout != 0)) {
    delay_microseconds_safe(200);
  }

  if(timeout > 0)
    return true;
  ESP_LOGE(TAG, "Timed out waiting for RX: 0x%02x", this->read_status(CC1101_MARCSTATE));
  return false;
}

bool Elero::wait_idle() {
  ESP_LOGVV(TAG, "wait_idle");
  uint8_t timeout = 200;
  while ((this->read_status(CC1101_MARCSTATE) != CC1101_MARCSTATE_IDLE) && (--timeout != 0)) {
    delay_microseconds_safe(200);
  }

  if(timeout > 0)
    return true;
  ESP_LOGE(TAG, "Timed out waiting for Idle: 0x%02x", this->read_status(CC1101_MARCSTATE));
  return false;
}

bool Elero::wait_tx() {
  ESP_LOGVV(TAG, "wait_tx");
  uint8_t timeout = 200;

  while ((this->read_status(CC1101_MARCSTATE) != CC1101_MARCSTATE_TX) && (--timeout != 0)) {
    delay_microseconds_safe(200);
  }

  if(timeout > 0)
    return true;
  ESP_LOGE(TAG, "Timed out waiting for TX: 0x%02x", this->read_status(CC1101_MARCSTATE));
  return false;
}

bool Elero::wait_tx_done() {
  ESP_LOGVV(TAG, "wait_tx_done");
  uint8_t timeout = 200;

  while((!this->received_) && (--timeout != 0)) {
    delay_microseconds_safe(200);
  }

  if(timeout > 0)
    return true;
  ESP_LOGE(TAG, "Timed out waiting for TX Done: 0x%02x", this->read_status(CC1101_MARCSTATE));
  return false;
}

bool Elero::transmit() {
  ESP_LOGVV(TAG, "transmit called for %d data bytes", this->msg_tx_[0]);

  // Go to IDLE first so the subsequent STX is not subject to CCA.
  // (STX from RX with MCSM1 CCA_MODE=3 requires a clear channel, which
  // fails when Elero motors are actively transmitting status replies.)
  this->write_cmd(CC1101_SIDLE);
  if (!this->wait_idle()) {
    this->flush_and_rx();
    return false;
  }

  // Flush TX FIFO before loading new data (required from IDLE state)
  this->write_cmd(CC1101_SFTX);
  delay_microseconds_safe(100);

  // Load TX FIFO
  this->write_burst(CC1101_TXFIFO, this->msg_tx_, this->msg_tx_[0] + 1);

  // Clear received_ so wait_tx_done() waits for the actual TX-end GDO0
  // falling edge, not a stale flag left over from a previously received packet.
  this->received_ = false;

  // Trigger TX — no CCA check when issuing STX from IDLE state
  this->write_cmd(CC1101_STX);

  if (!this->wait_tx()) {
    this->flush_and_rx();
    return false;
  }
  if (!this->wait_tx_done()) {
    this->flush_and_rx();
    return false;
  }

  uint8_t bytes = this->read_status(CC1101_TXBYTES) & 0x7f;
  if (bytes != 0) {
    ESP_LOGE(TAG, "Error transferring, %d bytes left in buffer", bytes);
    this->flush_and_rx();
    return false;
  }

  ESP_LOGV(TAG, "Transmission successful");
  this->flush_and_rx();  // return chip to clean RX state and clear received_
  return true;
}

uint8_t Elero::read_reg(uint8_t addr) {
  uint8_t data;

  this->enable();
  this->write_byte(addr | CC1101_READ_SINGLE);
  data = this->read_byte();
  this->disable();
  delay_microseconds_safe(15);
  // TODO: Add error handling - validate returned data, detect SPI communication failures
  return data;
}

uint8_t Elero::read_status(uint8_t addr) {
  uint8_t data;

  this->enable();
  this->write_byte(addr | CC1101_READ_BURST);
  data = this->read_byte();
  this->disable();
  delay_microseconds_safe(15);
  return data;
}

void Elero::read_buf(uint8_t addr, uint8_t *buf, uint8_t len) {
  this->enable();
  this->write_byte(addr | CC1101_READ_BURST);
  for(uint8_t i=0; i<len; i++)
    buf[i] = this->read_byte();
  this->disable();
  delay_microseconds_safe(15);
}

uint8_t Elero::count_bits(uint8_t byte)
{
  uint8_t i;
  uint8_t ones = 0;
  uint8_t mask = 1;

  for( i = 0; i < 8; i++ )
  {
    if( mask & byte )
    {
      ones += 1;
    }

    mask <<= 1;
  }

  return ones & 0x01;
}


void Elero::calc_parity(uint8_t* msg)
{
  uint8_t i;
  uint8_t p = 0;

  for( i = 0; i < 4; i++ )
  {
    uint8_t a = count_bits( msg[0 + i*2] );
    uint8_t b = count_bits( msg[1 + i*2] );

    p |= a ^ b;
    p <<= 1;
  }

  msg[7] = (p << 3);
}

void Elero::add_r20_to_nibbles(uint8_t* msg, uint8_t r20, uint8_t start, uint8_t length)
{
  uint8_t i;

  for( i = start; i < length; i++ )
  {
    uint8_t d = msg[i];

    uint8_t ln = (d + r20) & 0x0F;
    uint8_t hn = ((d & 0xF0) + (r20 & 0xF0)) & 0xFF;

    msg[i] = hn | ln;

    r20 = (r20 - 0x22) & 0xFF;
  }
}

void Elero::sub_r20_from_nibbles(uint8_t* msg, uint8_t r20, uint8_t start, uint8_t length)
{
  uint8_t i;

  for(i = start; i < length; i++)
  {
    uint8_t d = msg[i];

    uint8_t ln = (d - r20) & 0x0F;
    uint8_t hn = ((d & 0xF0) - (r20 & 0xF0)) & 0xFF;

    msg[i] = hn | ln;

    r20 = (r20 - 0x22) & 0xFF;
  }
}

void Elero::xor_2byte_in_array_encode(uint8_t* msg, uint8_t xor0, uint8_t xor1)
{
  uint8_t i;

  for( i = 1; i < 4; i++ )
  {
    msg[i*2 + 0] = msg[i*2 + 0] ^ xor0;
    msg[i*2 + 1] = msg[i*2 + 1] ^ xor1;
  }
}

void Elero::xor_2byte_in_array_decode(uint8_t* msg, uint8_t xor0, uint8_t xor1)
{
  uint8_t i;

  for( i = 0; i < 4; i++ )
  {
    msg[i*2 + 0] = msg[i*2 + 0] ^ xor0;
    msg[i*2 + 1] = msg[i*2 + 1] ^ xor1;
  }
}

void Elero::encode_nibbles(uint8_t* msg)
{
  uint8_t i;

  for( i = 0; i < 8; i++ )
  {
    uint8_t nh = (msg[i] >> 4) & 0x0F;
    uint8_t nl = msg[i] & 0x0F;

    uint8_t dh = flash_table_encode[nh];
    uint8_t dl = flash_table_encode[nl];

    msg[i] = ((dh << 4) & 0xFF) | ((dl) & 0xFF);
  }
}

void Elero::decode_nibbles(uint8_t* msg, uint8_t len)
{
  uint8_t i;

  for( i = 0; i < len; i++ )
  {
    uint8_t nh = (msg[i] >> 4) & 0x0F;
    uint8_t nl = msg[i] & 0x0F;

    uint8_t dh = flash_table_decode[nh];
    uint8_t dl = flash_table_decode[nl];

    msg[i] = ((dh << 4) & 0xFF) | ((dl) & 0xFF);
  }
}

void Elero::msg_decode(uint8_t *msg) {
  decode_nibbles(msg, 8);
  sub_r20_from_nibbles(msg, 0xFE, 0, 2);
  xor_2byte_in_array_decode(msg, msg[0], msg[1]);
  sub_r20_from_nibbles(msg, 0xBA, 2, 8);
}

void Elero::msg_encode(uint8_t* msg) {
  uint8_t xor0 = msg[0];
  uint8_t xor1 = msg[1];
  calc_parity(msg);
  add_r20_to_nibbles(msg, 0xFE, 0, 8);
  xor_2byte_in_array_encode(msg, xor0, xor1);
  encode_nibbles(msg);
}

void Elero::interpret_msg() {
  uint8_t length = this->msg_rx_[0];
  // Sanity check
  if(length > ELERO_MAX_PACKET_SIZE) {
    uint8_t dump_len = (length <= (uint8_t)(CC1101_FIFO_LENGTH - 3)) ? (length + 3) : CC1101_FIFO_LENGTH;
    ESP_LOGE(TAG, "Received invalid packet: too long (%d)", length);
    ESP_LOGD(TAG, "  Raw [%d bytes]: %s", dump_len,
             format_hex_pretty(this->msg_rx_, dump_len).c_str());
    if (this->packet_dump_pending_update_) {
      this->mark_last_raw_packet_(false, "too_long");
      this->packet_dump_pending_update_ = false;
    }
    return;
  }

  uint8_t cnt = this->msg_rx_[1];
  uint8_t typ = this->msg_rx_[2];
  uint8_t typ2 = this->msg_rx_[3];
  uint8_t hop = this->msg_rx_[4];
  uint8_t syst = this->msg_rx_[5];
  uint8_t chl = this->msg_rx_[6];
  uint32_t src = ((uint32_t)this->msg_rx_[7] << 16) | ((uint32_t)this->msg_rx_[8] << 8) | (this->msg_rx_[9]);
  uint32_t bwd = ((uint32_t)this->msg_rx_[10] << 16) | ((uint32_t)this->msg_rx_[11] << 8) | (this->msg_rx_[12]);
  uint32_t fwd = ((uint32_t)this->msg_rx_[13] << 16) | ((uint32_t)this->msg_rx_[14] << 8) | (this->msg_rx_[15]);
  uint8_t num_dests = this->msg_rx_[16];
  uint32_t dst;
  uint8_t dests_len;

  // Validate destination count before multiplication to prevent overflow
  if (num_dests > 20) {
    ESP_LOGE(TAG, "Received invalid packet: too many destinations (%d)", num_dests);
    ESP_LOGD(TAG, "  Raw [%d bytes]: %s", length + 3,
             format_hex_pretty(this->msg_rx_, length + 3).c_str());
    if (this->packet_dump_pending_update_) {
      this->mark_last_raw_packet_(false, "too_many_dests");
      this->packet_dump_pending_update_ = false;
    }
    return;
  }

  if(typ > 0x60) {
    dests_len = num_dests * 3;
    dst = ((uint32_t)this->msg_rx_[17] << 16) | ((uint32_t)this->msg_rx_[18] << 8) | (this->msg_rx_[19]);
  } else {
    dests_len = num_dests;
    dst = this->msg_rx_[17];
  }

  // Sanity check: msg_decode accesses 8 bytes at msg_rx_[19 + dests_len],
  // so the highest index touched is 26 + dests_len. This must be within both
  // the packet (length) and the FIFO buffer.
  if(26 + dests_len > length || 26 + dests_len >= CC1101_FIFO_LENGTH) {
    ESP_LOGE(TAG, "Received invalid packet: dests_len too long (%d) for length %d", dests_len, length);
    ESP_LOGD(TAG, "  Raw [%d bytes]: %s", length + 3,
             format_hex_pretty(this->msg_rx_, length + 3).c_str());
    if (this->packet_dump_pending_update_) {
      this->mark_last_raw_packet_(false, "dests_len_too_long");
      this->packet_dump_pending_update_ = false;
    }
    return;
  }

  // RSSI and LQI are appended by CC1101 after packet data at indices length+1 and length+2
  if (length + 2 >= CC1101_FIFO_LENGTH) {
    ESP_LOGE(TAG, "Received invalid packet: RSSI/LQI out of buffer bounds (length=%d)", length);
    ESP_LOGD(TAG, "  Raw [%d bytes]: %s", CC1101_FIFO_LENGTH,
             format_hex_pretty(this->msg_rx_, CC1101_FIFO_LENGTH).c_str());
    if (this->packet_dump_pending_update_) {
      this->mark_last_raw_packet_(false, "rssi_oob");
      this->packet_dump_pending_update_ = false;
    }
    return;
  }

  uint8_t payload1 = this->msg_rx_[17 + dests_len];
  uint8_t payload2 = this->msg_rx_[18 + dests_len];
  uint8_t crc = this->msg_rx_[length + 2] >> 7;
  uint8_t lqi = this->msg_rx_[length + 2] & 0x7f;

  // Calculate RSSI in dBm (CC1101 transmits as two's complement encoded value)
  float rssi;
  uint8_t rssi_raw = this->msg_rx_[length + 1];
  if (rssi_raw > ELERO_RSSI_SIGN_BIT) {
    // Negative value (two's complement): convert from two's complement
    rssi = static_cast<float>(static_cast<int8_t>(rssi_raw)) / ELERO_RSSI_DIVISOR + ELERO_RSSI_OFFSET;
  } else {
    // Positive value
    rssi = static_cast<float>(rssi_raw) / ELERO_RSSI_DIVISOR + ELERO_RSSI_OFFSET;
  }
  uint8_t *payload = &this->msg_rx_[19 + dests_len];
  msg_decode(payload);
  if (this->packet_dump_pending_update_) {
    this->mark_last_raw_packet_(true, nullptr);
    this->packet_dump_pending_update_ = false;
  }
  ESP_LOGD(TAG, "rcv'd: len=%02d, cnt=%02d, typ=0x%02x, typ2=0x%02x, hop=0x%02x, syst=0x%02x, chl=%02d, src=0x%06x, bwd=0x%06x, fwd=0x%06x, #dst=%02d, dst=0x%06x, rssi=%2.1f, lqi=%2d, crc=%2d, payload=[0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x]", length, cnt, typ, typ2, hop, syst, chl, src, bwd, fwd, num_dests, dst, rssi, lqi, crc, payload1, payload2, payload[0], payload[1], payload[2], payload[3], payload[4], payload[5], payload[6], payload[7]);

  // Update RSSI sensor for any message from a known blind
#ifdef USE_SENSOR
  {
    auto rssi_it = this->address_to_rssi_sensor_.find(src);
    if (rssi_it != this->address_to_rssi_sensor_.end()) {
      rssi_it->second->publish_state(rssi);
    }
  }
#endif

  // Track in discovery mode
  if (this->scan_mode_) {
    if ((typ == 0xca) || (typ == 0xc9)) {
      // Status response FROM the blind: src = blind addr, fwd = remote addr.
      // The params here (pck_inf, hop, channel, payload) belong to the blind's
      // response packet format — not to the command format we need to send.
      // Store them as a fallback (params_from_command=false); they will be
      // upgraded automatically once we see a matching 6a/69 command packet.
      this->track_discovered_blind(src, fwd, chl, typ, typ2, hop,
                                   payload1, payload2, rssi, payload[6], false);
    } else if ((typ == 0x6a) || (typ == 0x69)) {
      // Command FROM remote TO blind(s): src = remote addr.
      // The channel, pck_inf, hop and payload bytes here are exactly what must
      // be replicated when we send commands — iterate every destination and
      // register it as a discovered blind with the correct command params.
      for (uint8_t i = 0; i < num_dests; i++) {
        uint32_t dest_addr;
        if (typ > 0x60) {  // 3-byte addressing
          dest_addr = ((uint32_t)this->msg_rx_[17 + i * 3] << 16) |
                      ((uint32_t)this->msg_rx_[18 + i * 3] << 8) |
                      this->msg_rx_[19 + i * 3];
        } else {            // 1-byte addressing
          dest_addr = this->msg_rx_[17 + i];
        }
        this->track_discovered_blind(dest_addr, src, chl, typ, typ2, hop,
                                     payload1, payload2, rssi, 0, true);
      }
    }
  }

  if((typ == 0xca) || (typ == 0xc9)) { // Status message from a blind
    // Update text sensor
#ifdef USE_TEXT_SENSOR
    {
      auto text_it = this->address_to_text_sensor_.find(src);
      if (text_it != this->address_to_text_sensor_.end()) {
        text_it->second->publish_state(elero_state_to_string(payload[6]));
      }
    }
#endif

    // Check if we know the blind (configured ESPHome cover)
    auto search = this->address_to_cover_mapping_.find(src);
    if(search != this->address_to_cover_mapping_.end()) {
      search->second->notify_rx_meta(millis(), rssi);
      search->second->set_rx_state(payload[6]);
    }

    // Check if we know the address as a configured ESPHome light
    auto light_search = this->address_to_light_mapping_.find(src);
    if(light_search != this->address_to_light_mapping_.end()) {
      light_search->second->notify_rx_meta(millis(), rssi);
      light_search->second->set_rx_state(payload[6]);
    }

    // Update runtime adopted blinds
    auto it = this->runtime_blinds_.find(src);
    if (it != this->runtime_blinds_.end()) {
      it->second.last_seen_ms = millis();
      it->second.last_rssi = rssi;
      it->second.last_state = payload[6];
    }
  } else {
    // Non-status packets: still update RSSI/last_seen for any known blind
    auto search = this->address_to_cover_mapping_.find(src);
    if(search != this->address_to_cover_mapping_.end()) {
      search->second->notify_rx_meta(millis(), rssi);
    }
    auto light_search = this->address_to_light_mapping_.find(src);
    if(light_search != this->address_to_light_mapping_.end()) {
      light_search->second->notify_rx_meta(millis(), rssi);
    }
    auto rb_it = this->runtime_blinds_.find(src);
    if (rb_it != this->runtime_blinds_.end()) {
      rb_it->second.last_seen_ms = millis();
      rb_it->second.last_rssi = rssi;
    }
  }
}

void Elero::register_cover(EleroBlindBase *cover) {
  uint32_t address = cover->get_blind_address();
  if(this->address_to_cover_mapping_.find(address) != this->address_to_cover_mapping_.end()) {
    ESP_LOGE(TAG, "A blind with this address is already registered - this is currently not supported");
    return;
  }
  this->address_to_cover_mapping_.insert({address, cover});
  cover->set_poll_offset((this->address_to_cover_mapping_.size() - 1) * 5000);
}

void Elero::register_light(EleroLightBase *light) {
  uint32_t address = light->get_blind_address();
  if(this->address_to_light_mapping_.find(address) != this->address_to_light_mapping_.end()) {
    ESP_LOGE(TAG, "A light with this address is already registered - this is currently not supported");
    return;
  }
  this->address_to_light_mapping_.insert({address, light});
}

#ifdef USE_SENSOR
void Elero::register_rssi_sensor(uint32_t address, sensor::Sensor *sensor) {
  this->address_to_rssi_sensor_[address] = sensor;
}
#endif

#ifdef USE_TEXT_SENSOR
void Elero::register_text_sensor(uint32_t address, text_sensor::TextSensor *sensor) {
  this->address_to_text_sensor_[address] = sensor;
}
#endif

void Elero::start_packet_dump() {
  packet_dump_mode_ = true;
  ESP_LOGI(TAG, "Packet dump mode started");
}

void Elero::stop_packet_dump() {
  packet_dump_mode_ = false;
  ESP_LOGI(TAG, "Packet dump mode stopped");
}

void Elero::clear_raw_packets() {
  raw_packets_.clear();
  raw_packet_write_idx_ = 0;
}

void Elero::capture_raw_packet_(uint8_t fifo_len) {
  uint8_t actual_len = (fifo_len > CC1101_FIFO_LENGTH) ? CC1101_FIFO_LENGTH : fifo_len;
  RawPacket pkt{};
  pkt.timestamp_ms = millis();
  pkt.fifo_len = actual_len;
  memcpy(pkt.data, this->msg_rx_, actual_len);
  pkt.valid = false;
  pkt.reject_reason[0] = '\0';

  if (raw_packets_.size() < ELERO_MAX_RAW_PACKETS) {
    raw_packets_.push_back(pkt);
    raw_packet_write_idx_ = (uint8_t)(raw_packets_.size() - 1);
  } else {
    raw_packet_write_idx_ = (raw_packet_write_idx_ + 1) % ELERO_MAX_RAW_PACKETS;
    raw_packets_[raw_packet_write_idx_] = pkt;
  }
}

void Elero::mark_last_raw_packet_(bool valid, const char *reason) {
  if (raw_packets_.empty()) return;
  auto &pkt = raw_packets_[raw_packet_write_idx_];
  pkt.valid = valid;
  if (!valid && reason != nullptr) {
    strncpy(pkt.reject_reason, reason, sizeof(pkt.reject_reason) - 1);
    pkt.reject_reason[sizeof(pkt.reject_reason) - 1] = '\0';
  }
}

void Elero::track_discovered_blind(uint32_t src, uint32_t remote, uint8_t channel,
                                    uint8_t pck_inf0, uint8_t pck_inf1, uint8_t hop,
                                    uint8_t payload_1, uint8_t payload_2,
                                    float rssi, uint8_t state, bool from_command) {
  // Check if already tracked
  for (auto &blind : this->discovered_blinds_) {
    if (blind.blind_address == src) {
      blind.rssi = rssi;
      blind.last_seen = millis();
      if (state != 0) blind.last_state = state;
      blind.times_seen++;
      // Upgrade CA-derived params with command-packet params (higher quality):
      // a 6a/69 command packet tells us the exact format the remote uses, so
      // those values must be preferred over what the blind's own CA responses
      // carry (CA channel/hop/pck_inf describe the response format, not the
      // command format).
      if (from_command && !blind.params_from_command) {
        blind.remote_address = remote;
        blind.channel = channel;
        blind.pck_inf[0] = pck_inf0;
        blind.pck_inf[1] = pck_inf1;
        blind.hop = hop;
        blind.payload_1 = payload_1;
        blind.payload_2 = payload_2;
        blind.params_from_command = true;
        ESP_LOGI(TAG, "Upgraded blind 0x%06x params from command packet: ch=%d, pck_inf=0x%02x/0x%02x, hop=0x%02x",
                 src, channel, pck_inf0, pck_inf1, hop);
      }
      return;
    }
  }
  // Add new entry
  if (this->discovered_blinds_.size() < ELERO_MAX_DISCOVERED) {
    DiscoveredBlind blind{};
    blind.blind_address = src;
    blind.remote_address = remote;
    blind.channel = channel;
    blind.pck_inf[0] = pck_inf0;
    blind.pck_inf[1] = pck_inf1;
    blind.hop = hop;
    blind.payload_1 = payload_1;
    blind.payload_2 = payload_2;
    blind.rssi = rssi;
    blind.last_seen = millis();
    blind.last_state = state;
    blind.times_seen = 1;
    blind.params_from_command = from_command;
    this->discovered_blinds_.push_back(blind);
    ESP_LOGI(TAG, "Discovered new device: addr=0x%06x, remote=0x%06x, ch=%d, rssi=%.1f, src=%s",
             src, remote, channel, rssi, from_command ? "cmd_pkt" : "status_pkt");
  }
}

bool Elero::send_command(t_elero_command *cmd) {
  ESP_LOGVV(TAG, "send_command called");
  uint16_t code = (0x00 - (cmd->counter * ELERO_CRYPTO_MULT)) & ELERO_CRYPTO_MASK;
  this->msg_tx_[0] = ELERO_MSG_LENGTH;
  this->msg_tx_[1] = cmd->counter; // message counter
  this->msg_tx_[2] = cmd->pck_inf[0];
  this->msg_tx_[3] = cmd->pck_inf[1];
  this->msg_tx_[4] = cmd->hop; // hop info
  this->msg_tx_[5] = ELERO_SYS_ADDR;
  this->msg_tx_[6] = cmd->channel; // channel
  this->msg_tx_[7] = ((cmd->remote_addr >> 16) & 0xff); // source address
  this->msg_tx_[8] = ((cmd->remote_addr >> 8) & 0xff);
  this->msg_tx_[9] =((cmd->remote_addr) & 0xff);
  this->msg_tx_[10] = ((cmd->remote_addr >> 16) & 0xff); // backward address
  this->msg_tx_[11] = ((cmd->remote_addr >> 8) & 0xff);
  this->msg_tx_[12] =((cmd->remote_addr) & 0xff);
  this->msg_tx_[13] = ((cmd->remote_addr >> 16) & 0xff); // forward address
  this->msg_tx_[14] = ((cmd->remote_addr >> 8) & 0xff);
  this->msg_tx_[15] =((cmd->remote_addr) & 0xff);
  this->msg_tx_[16] = ELERO_DEST_COUNT;
  this->msg_tx_[17] = ((cmd->blind_addr >> 16) & 0xff); // blind address
  this->msg_tx_[18] = ((cmd->blind_addr >> 8) & 0xff);
  this->msg_tx_[19] = ((cmd->blind_addr) & 0xff);
  for(int i=0; i<10; i++)
    this->msg_tx_[20 + i] = cmd->payload[i];
  this->msg_tx_[22] = ((code >> 8) & 0xff);
  this->msg_tx_[23] = (code & 0xff);

  uint8_t *payload = &this->msg_tx_[22];
  msg_encode(payload);

  ESP_LOGV(TAG, "send: len=%02d, cnt=%02d, typ=0x%02x, typ2=0x%02x, hop=0x%02x, syst=0x%02x, chl=%02d, src=0x%02x%02x%02x, bwd=0x%02x%02x%02x, fwd=0x%02x%02x%02x, #dst=%02d, dst=0x%02x%02x%02x, payload=[0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x]", this->msg_tx_[0], this->msg_tx_[1], this->msg_tx_[2], this->msg_tx_[3], this->msg_tx_[4], this->msg_tx_[5], this->msg_tx_[6], this->msg_tx_[7], this->msg_tx_[8], this->msg_tx_[9], this->msg_tx_[10], this->msg_tx_[11], this->msg_tx_[12], this->msg_tx_[13], this->msg_tx_[14], this->msg_tx_[15], this->msg_tx_[16], this->msg_tx_[17], this->msg_tx_[18], this->msg_tx_[19], this->msg_tx_[20], this->msg_tx_[21], this->msg_tx_[22], this->msg_tx_[23], this->msg_tx_[24], this->msg_tx_[25], this->msg_tx_[26], this->msg_tx_[27], this->msg_tx_[28], this->msg_tx_[29]);
  return transmit();
}

// ─── Runtime blind adoption ───────────────────────────────────────────────

bool Elero::adopt_blind(const DiscoveredBlind &discovered, const std::string &name) {
  if (this->is_cover_configured(discovered.blind_address))
    return false;
  if (this->is_blind_adopted(discovered.blind_address))
    return false;
  RuntimeBlind rb{};
  rb.blind_address = discovered.blind_address;
  rb.remote_address = discovered.remote_address;
  rb.channel = discovered.channel;
  rb.pck_inf[0] = discovered.pck_inf[0];
  rb.pck_inf[1] = discovered.pck_inf[1];
  rb.hop = discovered.hop;
  rb.payload_1 = discovered.payload_1;
  rb.payload_2 = discovered.payload_2;
  rb.name = name.empty() ? "Adopted" : name;
  rb.last_seen_ms = discovered.last_seen;
  rb.last_rssi = discovered.rssi;
  rb.last_state = discovered.last_state;
  this->runtime_blinds_.insert({discovered.blind_address, std::move(rb)});
  ESP_LOGI(TAG, "Adopted runtime blind 0x%06x as \"%s\"",
           discovered.blind_address, rb.name.c_str());
  return true;
}

bool Elero::remove_runtime_blind(uint32_t addr) {
  auto it = this->runtime_blinds_.find(addr);
  if (it != this->runtime_blinds_.end()) {
    ESP_LOGI(TAG, "Removed runtime blind 0x%06x", addr);
    this->runtime_blinds_.erase(it);
    return true;
  }
  return false;
}

bool Elero::send_runtime_command(uint32_t addr, uint8_t cmd_byte) {
  auto it = this->runtime_blinds_.find(addr);
  if (it != this->runtime_blinds_.end()) {
    if (it->second.command_queue.size() < ELERO_MAX_COMMAND_QUEUE) {
      it->second.command_queue.push(cmd_byte);
      return true;
    }
    return false;  // Queue full
  }
  return false;
}

bool Elero::update_runtime_blind_settings(uint32_t addr, uint32_t open_dur_ms,
                                          uint32_t close_dur_ms, uint32_t poll_intvl_ms) {
  auto it = this->runtime_blinds_.find(addr);
  if (it != this->runtime_blinds_.end()) {
    it->second.open_duration_ms = open_dur_ms;
    it->second.close_duration_ms = close_dur_ms;
    it->second.poll_intvl_ms = poll_intvl_ms;
    return true;
  }
  return false;
}

bool Elero::is_blind_adopted(uint32_t addr) const {
  return this->runtime_blinds_.find(addr) != this->runtime_blinds_.end();
}

// ─── Log buffer ───────────────────────────────────────────────────────────

void Elero::append_log(uint8_t level, const char *tag, const char *fmt, ...) {
  if (!this->log_capture_) return;
  LogEntry entry{};
  entry.timestamp_ms = millis();
  entry.level = level;
  strncpy(entry.tag, tag, sizeof(entry.tag) - 1);
  va_list args;
  va_start(args, fmt);
  vsnprintf(entry.message, sizeof(entry.message), fmt, args);
  va_end(args);
  if (this->log_entries_.size() < ELERO_LOG_BUFFER_SIZE) {
    this->log_entries_.push_back(entry);
  } else {
    this->log_entries_[this->log_write_idx_] = entry;
    this->log_write_idx_ = (this->log_write_idx_ + 1) % ELERO_LOG_BUFFER_SIZE;
  }
}

}  // namespace elero
}  // namespace esphome
