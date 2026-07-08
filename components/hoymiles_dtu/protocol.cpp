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
         (static_cast<uint64_t>((serial >> 16) & 0xFF) << 16) | (static_cast<uint64_t>((serial >> 24) & 0xFF) << 8) |
         0x01ULL;
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

uint8_t hm_build_realtime_request(uint64_t inverter_radio_id, uint32_t dtu_serial, uint32_t timestamp, uint8_t *buffer,
                                  size_t buffer_len) {
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

uint8_t hm_build_power_limit_request(uint64_t inverter_radio_id, uint32_t dtu_serial, uint16_t percent, bool persistent,
                                     uint8_t *buffer, size_t buffer_len) {
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

namespace {

// Byte offsets of one DC channel's fields within the assembled realtime record. SHARED_DC_VOLTAGE
// means the input shares the previous channel's voltage reading (Ahoy CALC_UDC_CH: on 4-channel
// inverters the two inputs of an MPPT pair report a single voltage).
constexpr uint8_t SHARED_DC_VOLTAGE = 0xFF;
struct HmChannelLayout {
  uint8_t dc_voltage;
  uint8_t dc_current;
  uint8_t dc_power;
  uint8_t yield_today;
  uint8_t yield_total;
};

// Realtime record layout for a model family. Offsets mirror Ahoy src/hm/hmDefines.h
// hm{1,2,4}chAssignment (verified on-air against an HM-1200). min_len is the field-byte count that
// must be present before the record's trailing CRC16.
struct HmModelLayout {
  uint8_t channel_count;
  uint8_t min_len;
  uint8_t ac_voltage;
  uint8_t ac_current;
  uint8_t ac_power;
  uint8_t reactive_power;
  uint8_t ac_frequency;
  uint8_t power_factor;
  uint8_t temperature;
  uint8_t event_code;
  HmChannelLayout channels[HM_MAX_CHANNELS];
};

constexpr HmModelLayout HM_1CH_LAYOUT = {1, 30, 14, 22, 18, 20, 16, 24, 26, 28, {{2, 4, 6, 12, 8}, {}, {}, {}}};
constexpr HmModelLayout HM_2CH_LAYOUT = {
    2, 42, 26, 34, 30, 32, 28, 36, 38, 40, {{2, 4, 6, 22, 14}, {8, 10, 12, 24, 18}, {}, {}}};
constexpr HmModelLayout HM_4CH_LAYOUT = {
    4,
    62,
    46,
    54,
    50,
    52,
    48,
    56,
    58,
    60,
    {{2, 4, 8, 20, 12}, {SHARED_DC_VOLTAGE, 6, 10, 22, 16}, {24, 26, 30, 42, 34}, {SHARED_DC_VOLTAGE, 28, 32, 44, 38}}};

const HmModelLayout *hm_model_layout(HmModel model) {
  switch (model) {
    case HM_300:
    case HM_350:
    case HM_400:
      return &HM_1CH_LAYOUT;
    case HM_600:
    case HM_700:
    case HM_800:
      return &HM_2CH_LAYOUT;
    case HM_1000:
    case HM_1200:
    case HM_1500:
      return &HM_4CH_LAYOUT;
  }
  return nullptr;
}

}  // namespace

uint8_t hm_model_channel_count(HmModel model) {
  const HmModelLayout *layout = hm_model_layout(model);
  return layout == nullptr ? 0 : layout->channel_count;
}

bool hm_parse_realtime_payload(HmModel model, const uint8_t *payload, uint8_t len, HmTelemetry *telemetry) {
  // The assembled record is field data starting at byte 0 (it does NOT begin with the 0x0B command
  // byte, which only appears in the request). Per-model offsets come from the layout table above.
  const HmModelLayout *layout = hm_model_layout(model);
  if (payload == nullptr || telemetry == nullptr || layout == nullptr || len < layout->min_len) {
    return false;
  }
  HmTelemetry out;
  float previous_dc_voltage = 0.0f;
  for (uint8_t c = 0; c < layout->channel_count; c++) {
    const HmChannelLayout &ch = layout->channels[c];
    previous_dc_voltage =
        ch.dc_voltage == SHARED_DC_VOLTAGE ? previous_dc_voltage : read_u16_be(payload, ch.dc_voltage) / 10.0f;
    out.channels[c].dc_voltage = previous_dc_voltage;
    out.channels[c].dc_current = read_u16_be(payload, ch.dc_current) / 100.0f;
    out.channels[c].dc_power = read_u16_be(payload, ch.dc_power) / 10.0f;
    out.channels[c].yield_today = read_u16_be(payload, ch.yield_today);
    out.channels[c].yield_total = read_u32_be(payload, ch.yield_total) / 1000.0f;
    out.yield_today += out.channels[c].yield_today;
    out.yield_total += out.channels[c].yield_total;
  }
  out.ac_voltage = read_u16_be(payload, layout->ac_voltage) / 10.0f;
  out.ac_frequency = read_u16_be(payload, layout->ac_frequency) / 100.0f;
  out.ac_power = read_u16_be(payload, layout->ac_power) / 10.0f;
  out.reactive_power = read_u16_be(payload, layout->reactive_power) / 10.0f;
  out.ac_current = read_u16_be(payload, layout->ac_current) / 100.0f;
  out.power_factor = read_u16_be(payload, layout->power_factor) / 1000.0f;
  out.temperature = static_cast<int16_t>(read_u16_be(payload, layout->temperature)) / 10.0f;
  out.event_code = read_u16_be(payload, layout->event_code);
  out.valid = true;
  *telemetry = out;
  return true;
}

const char *hm_model_to_string(HmModel model) {
  switch (model) {
    case HM_300:
      return "HM-300";
    case HM_350:
      return "HM-350";
    case HM_400:
      return "HM-400";
    case HM_600:
      return "HM-600";
    case HM_700:
      return "HM-700";
    case HM_800:
      return "HM-800";
    case HM_1000:
      return "HM-1000";
    case HM_1200:
      return "HM-1200";
    case HM_1500:
      return "HM-1500";
  }
  return "unknown";
}

bool hm_classify_sniffed_packet(const uint8_t *packet, uint8_t len, uint64_t our_inverter_radio_id,
                                uint32_t our_dtu_serial, HmSniffResult *out) {
  if (packet == nullptr || out == nullptr || len < 11 || len > 32) {
    return false;
  }
  // A genuine DTU->inverter request ends in a CRC8 over the preceding bytes (verified against the
  // report's on-air Search-ID and collect-info captures). This rejects RF noise and any inverter
  // response that happens to land on a monitored pipe.
  if (hm_crc8(packet, len - 1) != packet[len - 1]) {
    return false;
  }
  const uint8_t opcode = packet[0];
  // Inverter->DTU responses set bit7 (e.g. 0x95, 0xD1). We only flag requests (bit7 clear).
  if ((opcode & 0x80) != 0) {
    return false;
  }
  // [1..4] is the target inverter id, LSB-first of (radio_id >> 8) == inverter address[1..4].
  bool targets_our_inverter = true;
  for (uint8_t i = 0; i < 4; i++) {
    if (packet[1 + i] != ((our_inverter_radio_id >> (8 * (i + 1))) & 0xFF)) {
      targets_our_inverter = false;
      break;
    }
  }

  HmSniffResult result;
  result.opcode = opcode;
  result.targets_our_inverter = targets_our_inverter;
  switch (opcode) {
    case HM_TX_SEARCH_ID:  // broadcast discovery; sender DTU serial sits at [5..8]
      result.kind = HM_SNIFF_SEARCH_ID;
      result.sender_dtu_serial = read_u32_be(packet, 5);
      break;
    case HM_TX_COLLECT_INFO:  // one byte longer than Search-ID; sender DTU serial sits at [6..9]
      if (len < 12) {
        return false;
      }
      result.kind = HM_SNIFF_COLLECT_INFO;
      result.sender_dtu_serial = read_u32_be(packet, 6);
      break;
    case HM_TX_REQ_INFO:  // a telemetry poll of our inverter from someone other than us
      if (!targets_our_inverter) {
        return false;
      }
      result.kind = HM_SNIFF_FOREIGN_POLL;
      result.sender_dtu_serial = read_u32_be(packet, 5);
      break;
    case HM_TX_REQ_DEVCONTROL:  // a control command (power limit / on-off) aimed at our inverter
      if (!targets_our_inverter) {
        return false;
      }
      result.kind = HM_SNIFF_FOREIGN_CONTROL;
      result.sender_dtu_serial = read_u32_be(packet, 5);
      break;
    default:
      return false;
  }
  // A request carrying our own DTU serial is our traffic, not an intruder (should not occur while
  // monitoring, but guard against reflections/misconfiguration).
  if (our_dtu_serial != 0 && result.sender_dtu_serial == our_dtu_serial) {
    return false;
  }
  *out = result;
  return true;
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
