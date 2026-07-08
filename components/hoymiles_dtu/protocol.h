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
static constexpr uint8_t HM_TX_REQ_DEVCONTROL = 0x51;
// Discovery/probe opcodes an attacker uses to harvest inverter serials over the air (see the CCC
// "Wireless Interface Vulnerabilities of Hoymiles Microinverters" report, docs/).
static constexpr uint8_t HM_TX_SEARCH_ID = 0x02;     // "Search ID" (Gongfa) broadcast discovery
static constexpr uint8_t HM_TX_COLLECT_INFO = 0x06;  // Collect RF hw/sw info (leaks serial too)
// The HM "global" Search-ID listening address (report section 4.2). A DTU broadcasts Search-ID
// requests to this address to enumerate inverter serials; listening on it detects such scans.
static constexpr uint8_t HM_SEARCH_ID_ADDRESS[5] = {0x05, 0x64, 0x64, 0x64, 0x64};
static constexpr uint8_t HM_ALL_FRAMES = 0x80;
static constexpr uint8_t HM_SINGLE_FRAME = 0x81;
static constexpr uint8_t HM_REAL_TIME_RUN_DATA_DEBUG = 0x0B;
static constexpr uint8_t HM_DEV_CTRL_ACTIVE_POWER = 0x0B;  // ActivePowerContr (=11)

// Supported HM-series microinverters, grouped by DC-input (channel) count. This mirrors the model
// families Ahoy handles over nRF24: 1-channel HM-300/350/400, 2-channel HM-600/700/800, and
// 4-channel HM-1000/1200/1500. The realtime payload layout depends only on the channel count.
enum HmModel : uint8_t {
  HM_300,
  HM_350,
  HM_400,
  HM_600,
  HM_700,
  HM_800,
  HM_1000,
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
  float reactive_power{0.0f};
  float power_factor{0.0f};
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
uint8_t hm_build_realtime_request(uint64_t inverter_radio_id, uint32_t dtu_serial, uint32_t timestamp, uint8_t *buffer,
                                  size_t buffer_len);
// Active-power-limit DevControl request. percent is 0..100; persistent=true survives an inverter
// restart (RelativPersistent), false is RelativNonPersistent. Returns packet length or 0 on error.
uint8_t hm_build_power_limit_request(uint64_t inverter_radio_id, uint32_t dtu_serial, uint16_t percent, bool persistent,
                                     uint8_t *buffer, size_t buffer_len);
bool hm_parse_frame(const uint8_t *packet, uint8_t len, uint64_t inverter_radio_id, HmFrame *frame);
bool hm_assemble_payload(const HmFrame *frames, uint8_t frame_count, uint8_t *payload, size_t payload_len,
                         uint8_t *assembled_len);
// Number of DC channels this model reports (1, 2, or 4).
uint8_t hm_model_channel_count(HmModel model);
// Decode an assembled realtime record into telemetry. Field byte offsets follow the model's channel
// count (Ahoy hm{1,2,4}chAssignment). Returns false on a null/too-short payload or unknown model.
bool hm_parse_realtime_payload(HmModel model, const uint8_t *payload, uint8_t len, HmTelemetry *telemetry);
const char *hm_model_to_string(HmModel model);
const char *hm_status_to_string(HmStatus status);

// --- Passive scan/intrusion detection (receive-only) ---------------------------------------------
// Classification of a passively-overheard DTU->inverter request. Every such request must reach the
// inverter over its own (publicly derivable) address, so a monitor tuned to that address and to the
// global Search-ID address can overhear an attacker's traffic without transmitting anything.
enum HmSniffKind : uint8_t {
  HM_SNIFF_NONE = 0,
  HM_SNIFF_SEARCH_ID,        // 0x02 broadcast discovery — the mass-scan signature
  HM_SNIFF_COLLECT_INFO,     // 0x06 RF info probe — leaks the serial even while a DTU is bound
  HM_SNIFF_FOREIGN_POLL,     // 0x15 telemetry request aimed at our inverter by a foreign DTU
  HM_SNIFF_FOREIGN_CONTROL,  // 0x51 DevControl aimed at our inverter by a foreign DTU (most serious)
};

struct HmSniffResult {
  HmSniffKind kind{HM_SNIFF_NONE};
  uint8_t opcode{0};
  uint32_t sender_dtu_serial{0};  // the attacker's DTU serial embedded in the request (0 if unknown)
  bool targets_our_inverter{false};
};

// Classify a passively-received packet as a foreign DTU request (scan/probe/control), or reject it.
// Validates the trailing app-layer CRC8 so RF noise and inverter responses (opcode bit7 set) are
// ignored, and drops requests that carry our own DTU serial. Returns true and fills *out only for a
// recognised foreign request.
bool hm_classify_sniffed_packet(const uint8_t *packet, uint8_t len, uint64_t our_inverter_radio_id,
                                uint32_t our_dtu_serial, HmSniffResult *out);

}  // namespace hoymiles_dtu
}  // namespace esphome
