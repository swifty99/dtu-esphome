#include "protocol.h"

#include "esphome/core/hal.h"

#include <cstring>

namespace esphome {
namespace hoymiles_dtu {

static uint16_t read_u16_be(const uint8_t *data, size_t offset) {
  return (static_cast<uint16_t>(data[offset]) << 8) | data[offset + 1];
}

static uint32_t read_u32_be(const uint8_t *data, size_t offset) {
  return (static_cast<uint32_t>(data[offset]) << 24) | (static_cast<uint32_t>(data[offset + 1]) << 16) |
         (static_cast<uint32_t>(data[offset + 2]) << 8) | data[offset + 3];
}

static void write_hm_u32_le(uint8_t *data, uint32_t value) {
  data[0] = value & 0xFF;
  data[1] = (value >> 8) & 0xFF;
  data[2] = (value >> 16) & 0xFF;
  data[3] = (value >> 24) & 0xFF;
}

static void write_hm_u32_be(uint8_t *data, uint32_t value) {
  data[0] = (value >> 24) & 0xFF;
  data[1] = (value >> 16) & 0xFF;
  data[2] = (value >> 8) & 0xFF;
  data[3] = value & 0xFF;
}

uint8_t hm_crc8(const uint8_t *data, size_t len) {
  uint8_t crc = 0;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      if ((crc & 0x80) != 0) {
        crc = static_cast<uint8_t>((crc << 1) ^ 0x01);
      } else {
        crc = static_cast<uint8_t>(crc << 1);
      }
    }
  }
  return crc;
}

uint16_t hm_crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      if ((crc & 0x0001) != 0) {
        crc = static_cast<uint16_t>((crc >> 1) ^ 0xA001);
      } else {
        crc = static_cast<uint16_t>(crc >> 1);
      }
    }
  }
  return crc;
}

uint64_t hm_radio_id_from_serial(uint64_t serial) {
  return (static_cast<uint64_t>(serial & 0xFF) << 32) | (static_cast<uint64_t>((serial >> 8) & 0xFF) << 24) |
         (static_cast<uint64_t>((serial >> 16) & 0xFF) << 16) |
         (static_cast<uint64_t>((serial >> 24) & 0xFF) << 8) | 0x01ULL;
}

void hm_radio_id_to_address(uint64_t radio_id, uint8_t *address) {
  for (uint8_t i = 0; i < 5; i++) {
    address[i] = (radio_id >> (8 * i)) & 0xFF;
  }
}

uint32_t hm_generate_dtu_serial() {
  uint8_t mac[6] = {};
  get_mac_address_raw(mac);
  uint32_t chip_id = (static_cast<uint32_t>(mac[2]) << 24) | (static_cast<uint32_t>(mac[3]) << 16) |
                     (static_cast<uint32_t>(mac[4]) << 8) | mac[5];
  uint32_t dtu_serial = 0x80000000UL;
  for (uint8_t i = 0; i < 28; i += 4) {
    uint8_t nibble = (chip_id >> i) & 0x0F;
    if (nibble > 9) {
      nibble -= 6;
    }
    dtu_serial |= static_cast<uint32_t>(nibble) << i;
  }
  return dtu_serial;
}

uint8_t hm_build_realtime_request(uint64_t inverter_radio_id, uint32_t dtu_serial, uint32_t timestamp,
                                  uint8_t *buffer, size_t buffer_len) {
  if (buffer_len < 27) {
    return 0;
  }
  memset(buffer, 0, 27);
  buffer[0] = HM_TX_REQ_INFO;
  // On-air byte order must match Ahoy exactly, or the inverter ACKs at the radio layer but rejects
  // the request at the app layer (it checks [1..4] against its own id) and routes its reply (to the
  // address derived from [5..8]) to somewhere we are not listening. The address bytes are
  // `0x01 + idbytes`, so [1..4] = inverter_address[1..4] = LSB-first of (radio_id>>8), and
  // [5..8] = dtu_address[1..4] = big-endian dtu_serial. (Ahoy's CP_U32_* macros are misnamed; this
  // reproduces their actual output, verified against a live capture: inverter id `82 80 69 89`.)
  write_hm_u32_le(&buffer[1], static_cast<uint32_t>(inverter_radio_id >> 8));
  write_hm_u32_be(&buffer[5], dtu_serial);
  buffer[9] = HM_ALL_FRAMES;
  buffer[10] = HM_REAL_TIME_RUN_DATA_DEBUG;
  // Timestamp is transmitted big-endian (MSB first). Confirmed two ways: a live on-air
  // capture of Ahoy (`6A 43 90 09` = a valid epoch) and Ahoy's source, where the
  // misleadingly-named CP_U32_LittleEndian macro actually writes MSB-first (helper.h).
  write_hm_u32_be(&buffer[12], timestamp);
  uint8_t len = 24;
  const uint16_t crc16 = hm_crc16(&buffer[10], len - 10);
  buffer[len++] = (crc16 >> 8) & 0xFF;
  buffer[len++] = crc16 & 0xFF;
  buffer[len] = hm_crc8(buffer, len);
  len++;
  return len;
}

uint8_t hm_build_power_limit_request(uint64_t inverter_radio_id, uint32_t dtu_serial, uint16_t percent,
                                     bool persistent, uint8_t *buffer, size_t buffer_len) {
  if (buffer_len < 19 || percent > 100) {
    return 0;
  }
  memset(buffer, 0, 19);
  buffer[0] = HM_TX_REQ_DEVCONTROL;
  // Same id byte order as the realtime request (see hm_build_realtime_request).
  write_hm_u32_le(&buffer[1], static_cast<uint32_t>(inverter_radio_id >> 8));
  write_hm_u32_be(&buffer[5], dtu_serial);
  buffer[9] = HM_SINGLE_FRAME;
  buffer[10] = HM_DEV_CTRL_ACTIVE_POWER;
  buffer[11] = 0x00;
  const uint16_t limit = static_cast<uint16_t>(percent * 10);  // relative %, transmitted ×10
  buffer[12] = (limit >> 8) & 0xFF;
  buffer[13] = limit & 0xFF;
  const uint16_t control = persistent ? 0x0101 : 0x0001;  // RelativPersistent / RelativNonPersistent
  buffer[14] = (control >> 8) & 0xFF;
  buffer[15] = control & 0xFF;
  uint8_t len = 16;
  const uint16_t crc16 = hm_crc16(&buffer[10], len - 10);
  buffer[len++] = (crc16 >> 8) & 0xFF;
  buffer[len++] = crc16 & 0xFF;
  buffer[len] = hm_crc8(buffer, len);
  len++;
  return len;
}

bool hm_parse_frame(const uint8_t *packet, uint8_t len, uint64_t inverter_radio_id, HmFrame *frame) {
  if (packet == nullptr || frame == nullptr || len < 12) {
    return false;
  }
  if (packet[0] != (HM_TX_REQ_INFO + HM_ALL_FRAMES)) {
    return false;
  }
  if (hm_crc8(packet, len - 1) != packet[len - 1]) {
    return false;
  }
  for (uint8_t i = 0; i < 4; i++) {
    if (packet[1 + i] != ((inverter_radio_id >> (8 * (i + 1))) & 0xFF)) {
      return false;
    }
  }
  const uint8_t frame_id = packet[9] & 0x7F;
  if (frame_id == 0 || frame_id > HM_MAX_FRAME_COUNT) {
    return false;
  }
  frame->id = frame_id;
  frame->len = len - 11;
  memset(frame->data.data(), 0, frame->data.size());
  memcpy(frame->data.data(), &packet[10], frame->len);
  return true;
}

bool hm_assemble_payload(const HmFrame *frames, uint8_t frame_count, uint8_t *payload, size_t payload_len,
                         uint8_t *assembled_len) {
  if (frames == nullptr || payload == nullptr || assembled_len == nullptr || frame_count == 0) {
    return false;
  }
  uint8_t offset = 0;
  for (uint8_t id = 1; id <= frame_count; id++) {
    const HmFrame *found = nullptr;
    for (uint8_t i = 0; i < frame_count; i++) {
      if (frames[i].id == id) {
        found = &frames[i];
        break;
      }
    }
    if (found == nullptr || found->len == 0) {
      return false;
    }
    if (offset + found->len > payload_len) {
      return false;
    }
    memcpy(&payload[offset], found->data.data(), found->len);
    offset += found->len;
  }
  *assembled_len = offset;
  return true;
}

bool hm_parse_4ch_payload(const uint8_t *payload, uint8_t len, HmTelemetry *telemetry) {
  // The assembled record is field data starting at byte 0 (offsets below match Ahoy's
  // hm4chAssignment); it does NOT begin with the 0x0B command byte (that is only in the request).
  // HM4CH record is 62 field bytes + a trailing CRC16, so 62 is the minimum length.
  if (payload == nullptr || telemetry == nullptr || len < 62) {
    return false;
  }
  HmTelemetry out;
  out.channels[0].dc_voltage = read_u16_be(payload, 2) / 10.0f;
  out.channels[0].dc_current = read_u16_be(payload, 4) / 100.0f;
  out.channels[0].dc_power = read_u16_be(payload, 8) / 10.0f;
  out.channels[0].yield_today = read_u16_be(payload, 20);
  out.channels[0].yield_total = read_u32_be(payload, 12) / 1000.0f;

  out.channels[1].dc_voltage = out.channels[0].dc_voltage;
  out.channels[1].dc_current = read_u16_be(payload, 6) / 100.0f;
  out.channels[1].dc_power = read_u16_be(payload, 10) / 10.0f;
  out.channels[1].yield_today = read_u16_be(payload, 22);
  out.channels[1].yield_total = read_u32_be(payload, 16) / 1000.0f;

  out.channels[2].dc_voltage = read_u16_be(payload, 24) / 10.0f;
  out.channels[2].dc_current = read_u16_be(payload, 26) / 100.0f;
  out.channels[2].dc_power = read_u16_be(payload, 30) / 10.0f;
  out.channels[2].yield_today = read_u16_be(payload, 42);
  out.channels[2].yield_total = read_u32_be(payload, 34) / 1000.0f;

  out.channels[3].dc_voltage = out.channels[2].dc_voltage;
  out.channels[3].dc_current = read_u16_be(payload, 28) / 100.0f;
  out.channels[3].dc_power = read_u16_be(payload, 32) / 10.0f;
  out.channels[3].yield_today = read_u16_be(payload, 44);
  out.channels[3].yield_total = read_u32_be(payload, 38) / 1000.0f;

  out.ac_voltage = read_u16_be(payload, 46) / 10.0f;
  out.ac_frequency = read_u16_be(payload, 48) / 100.0f;
  out.ac_power = read_u16_be(payload, 50) / 10.0f;
  out.reactive_power = read_u16_be(payload, 52) / 10.0f;
  out.ac_current = read_u16_be(payload, 54) / 100.0f;
  out.power_factor = read_u16_be(payload, 56) / 1000.0f;
  out.temperature = static_cast<int16_t>(read_u16_be(payload, 58)) / 10.0f;
  out.event_code = read_u16_be(payload, 60);
  for (const auto &channel : out.channels) {
    out.yield_today += channel.yield_today;
    out.yield_total += channel.yield_total;
  }
  out.valid = true;
  *telemetry = out;
  return true;
}

const char *hm_model_to_string(HmModel model) {
  switch (model) {
    case HM_1200:
      return "HM-1200";
    case HM_1500:
      return "HM-1500";
  }
  return "unknown";
}

const char *hm_status_to_string(HmStatus status) {
  switch (status) {
    case OFFLINE:
      return "offline";
    case ONLINE:
      return "online";
    case PRODUCING:
      return "producing";
  }
  return "unknown";
}

}  // namespace hoymiles_dtu
}  // namespace esphome
