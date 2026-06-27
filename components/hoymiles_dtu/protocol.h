#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome {
namespace hoymiles_dtu {

static constexpr uint8_t HM_MAX_CHANNELS = 4;
static constexpr uint8_t HM_MAX_FRAME_COUNT = 8;
static constexpr uint8_t HM_MAX_PAYLOAD_SIZE = 96;
static constexpr uint8_t HM_RF_CHANNELS[5] = {3, 23, 40, 61, 75};

static constexpr uint8_t HM_TX_REQ_INFO = 0x15;
static constexpr uint8_t HM_ALL_FRAMES = 0x80;
static constexpr uint8_t HM_REAL_TIME_RUN_DATA_DEBUG = 0x0B;

enum HmModel : uint8_t {
  HM_1200,
  HM_1500,
};

enum HmStatus : uint8_t {
  OFFLINE,
  ONLINE,
  PRODUCING,
};

struct HmChannelData {
  float dc_voltage{0.0f};
  float dc_current{0.0f};
  float dc_power{0.0f};
  float yield_today{0.0f};
  float yield_total{0.0f};
};

struct HmTelemetry {
  bool valid{false};
  float ac_voltage{0.0f};
  float ac_current{0.0f};
  float ac_power{0.0f};
  float ac_frequency{0.0f};
  float temperature{0.0f};
  float yield_today{0.0f};
  float yield_total{0.0f};
  uint16_t event_code{0};
  std::array<HmChannelData, HM_MAX_CHANNELS> channels{};
};

struct HmFrame {
  uint8_t id{0};
  uint8_t len{0};
  std::array<uint8_t, 32> data{};
};

uint8_t hm_crc8(const uint8_t *data, size_t len);
uint16_t hm_crc16(const uint8_t *data, size_t len);
uint64_t hm_radio_id_from_serial(uint64_t serial);
void hm_radio_id_to_address(uint64_t radio_id, uint8_t *address);
uint32_t hm_generate_dtu_serial();
uint8_t hm_build_realtime_request(uint64_t inverter_radio_id, uint32_t dtu_serial, uint32_t timestamp,
                                  uint8_t *buffer, size_t buffer_len);
bool hm_parse_frame(const uint8_t *packet, uint8_t len, uint64_t inverter_radio_id, HmFrame *frame);
bool hm_assemble_payload(const HmFrame *frames, uint8_t frame_count, uint8_t *payload, size_t payload_len,
                         uint8_t *assembled_len);
bool hm_parse_4ch_payload(const uint8_t *payload, uint8_t len, HmTelemetry *telemetry);
const char *hm_model_to_string(HmModel model);
const char *hm_status_to_string(HmStatus status);

}  // namespace hoymiles_dtu
}  // namespace esphome
