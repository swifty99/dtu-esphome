#include "hoymiles_dtu.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <cstdio>
#include <cstring>

namespace esphome {
namespace hoymiles_dtu {

static const char *const TAG = "hoymiles_dtu";

static constexpr uint8_t NRF_R_REGISTER = 0x00;
static constexpr uint8_t NRF_W_REGISTER = 0x20;
static constexpr uint8_t NRF_R_RX_PAYLOAD = 0x61;
static constexpr uint8_t NRF_W_TX_PAYLOAD = 0xA0;
static constexpr uint8_t NRF_FLUSH_TX = 0xE1;
static constexpr uint8_t NRF_FLUSH_RX = 0xE2;
static constexpr uint8_t NRF_ACTIVATE = 0x50;
static constexpr uint8_t NRF_R_RX_PL_WID = 0x60;
static constexpr uint8_t NRF_NOP = 0xFF;

static constexpr uint8_t NRF_REG_CONFIG = 0x00;
static constexpr uint8_t NRF_REG_EN_AA = 0x01;
static constexpr uint8_t NRF_REG_EN_RXADDR = 0x02;
static constexpr uint8_t NRF_REG_SETUP_AW = 0x03;
static constexpr uint8_t NRF_REG_SETUP_RETR = 0x04;
static constexpr uint8_t NRF_REG_RF_CH = 0x05;
static constexpr uint8_t NRF_REG_RF_SETUP = 0x06;
static constexpr uint8_t NRF_REG_STATUS = 0x07;
static constexpr uint8_t NRF_REG_RX_ADDR_P0 = 0x0A;
static constexpr uint8_t NRF_REG_RX_ADDR_P1 = 0x0B;
static constexpr uint8_t NRF_REG_TX_ADDR = 0x10;
static constexpr uint8_t NRF_REG_FIFO_STATUS = 0x17;
static constexpr uint8_t NRF_REG_DYNPD = 0x1C;
static constexpr uint8_t NRF_REG_FEATURE = 0x1D;

static constexpr uint8_t NRF_CONFIG_EN_CRC = 0x08;
static constexpr uint8_t NRF_CONFIG_CRCO = 0x04;
static constexpr uint8_t NRF_CONFIG_PWR_UP = 0x02;
static constexpr uint8_t NRF_CONFIG_PRIM_RX = 0x01;
static constexpr uint8_t NRF_STATUS_RX_DR = 0x40;
static constexpr uint8_t NRF_STATUS_TX_DS = 0x20;
static constexpr uint8_t NRF_STATUS_MAX_RT = 0x10;
static constexpr uint8_t NRF_FIFO_RX_EMPTY = 0x01;

static constexpr uint32_t TX_TIMEOUT_MS = 35;
static constexpr uint32_t RX_TIMEOUT_MS = 520;
static constexpr uint32_t RX_CHANNEL_DWELL_MS = 8;

void HoymilesDtuInverter::set_serial(uint64_t serial) {
  serial_ = serial;
  radio_id_ = hm_radio_id_from_serial(serial);
}

void HoymilesDtuInverter::set_channel_dc_voltage_sensor(uint8_t channel, sensor::Sensor *sensor) {
  if (channel >= 1 && channel <= HM_MAX_CHANNELS) {
    channels_[channel - 1].dc_voltage = sensor;
  }
}

void HoymilesDtuInverter::set_channel_dc_current_sensor(uint8_t channel, sensor::Sensor *sensor) {
  if (channel >= 1 && channel <= HM_MAX_CHANNELS) {
    channels_[channel - 1].dc_current = sensor;
  }
}

void HoymilesDtuInverter::set_channel_dc_power_sensor(uint8_t channel, sensor::Sensor *sensor) {
  if (channel >= 1 && channel <= HM_MAX_CHANNELS) {
    channels_[channel - 1].dc_power = sensor;
  }
}

void HoymilesDtuInverter::set_channel_yield_today_sensor(uint8_t channel, sensor::Sensor *sensor) {
  if (channel >= 1 && channel <= HM_MAX_CHANNELS) {
    channels_[channel - 1].yield_today = sensor;
  }
}

bool HoymilesDtuInverter::due(uint32_t now, uint32_t poll_interval) const {
  return last_poll_ms_ == 0 || now - last_poll_ms_ >= poll_interval;
}

void HoymilesDtuInverter::publish_telemetry(const HmTelemetry &telemetry, uint32_t now) {
  telemetry_ = telemetry;
  last_seen_ms_ = now;
  last_error_ = "";
  status_ = telemetry.ac_power > 0.1f ? PRODUCING : ONLINE;

  if (ac_power_sensor_ != nullptr) {
    ac_power_sensor_->publish_state(telemetry.ac_power);
  }
  if (ac_voltage_sensor_ != nullptr) {
    ac_voltage_sensor_->publish_state(telemetry.ac_voltage);
  }
  if (ac_current_sensor_ != nullptr) {
    ac_current_sensor_->publish_state(telemetry.ac_current);
  }
  if (ac_frequency_sensor_ != nullptr) {
    ac_frequency_sensor_->publish_state(telemetry.ac_frequency);
  }
  if (temperature_sensor_ != nullptr) {
    temperature_sensor_->publish_state(telemetry.temperature);
  }
  if (yield_today_sensor_ != nullptr) {
    yield_today_sensor_->publish_state(telemetry.yield_today);
  }
  if (yield_total_sensor_ != nullptr) {
    yield_total_sensor_->publish_state(telemetry.yield_total);
  }
  for (uint8_t i = 0; i < HM_MAX_CHANNELS; i++) {
    const auto &data = telemetry.channels[i];
    const auto &sensors = channels_[i];
    if (sensors.dc_voltage != nullptr) {
      sensors.dc_voltage->publish_state(data.dc_voltage);
    }
    if (sensors.dc_current != nullptr) {
      sensors.dc_current->publish_state(data.dc_current);
    }
    if (sensors.dc_power != nullptr) {
      sensors.dc_power->publish_state(data.dc_power);
    }
    if (sensors.yield_today != nullptr) {
      sensors.yield_today->publish_state(data.yield_today);
    }
  }
  publish_status_();
}

void HoymilesDtuInverter::mark_offline_if_expired(uint32_t now, uint32_t timeout_ms) {
  if (status_ == OFFLINE || last_seen_ms_ == 0 || now - last_seen_ms_ < timeout_ms) {
    return;
  }
  status_ = OFFLINE;
  last_error_ = "offline timeout";
  publish_status_();
}

void HoymilesDtuInverter::publish_status_() {
  if (status_text_sensor_ != nullptr) {
    status_text_sensor_->publish_state(hm_status_to_string(status_));
  }
  if (last_seen_text_sensor_ != nullptr && last_seen_ms_ != 0) {
    char buffer[24];
    snprintf(buffer, sizeof(buffer), "%us", last_seen_ms_ / 1000);
    last_seen_text_sensor_->publish_state(buffer);
  }
}

void HoymilesDtuComponent::setup() {
  if (ce_pin_ == nullptr) {
    ESP_LOGE(TAG, "CE pin is not configured");
    mark_failed();
    return;
  }
  ce_pin_->setup();
  ce_pin_->digital_write(false);
  if (irq_pin_ != nullptr) {
    irq_pin_->setup();
  }

  spi_setup();
  if (!dtu_serial_configured_) {
    dtu_serial_ = hm_generate_dtu_serial();
  }
  radio_ok_ = setup_radio_();
  if (!radio_ok_) {
    ESP_LOGE(TAG, "nRF24 radio probe failed: STATUS=0x%02X CONFIG=0x%02X SETUP_AW=0x%02X", detected_status_,
             detected_config_, detected_setup_aw_);
    mark_failed();
    return;
  }
  ESP_LOGCONFIG(TAG, "nRF24 radio ready, DTU serial 0x%08X", dtu_serial_);
}

void HoymilesDtuComponent::loop() {
  if (!radio_ok_) {
    return;
  }
  const uint32_t now = millis();
  for (auto *inverter : inverters_) {
    inverter->mark_offline_if_expired(now, poll_interval_ms_ * 4);
  }

  switch (request_state_) {
    case RequestState::IDLE:
      for (auto *inverter : inverters_) {
        if (inverter->due(now, poll_interval_ms_)) {
          start_request_(inverter, now);
          break;
        }
      }
      break;
    case RequestState::WAIT_TX:
      poll_tx_(now);
      break;
    case RequestState::WAIT_RX:
      poll_rx_(now);
      break;
  }
}

void HoymilesDtuComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Hoymiles DTU:");
  LOG_PIN("  CS Pin: ", this->cs_);
  LOG_PIN("  CE Pin: ", this->ce_pin_);
  LOG_PIN("  IRQ Pin: ", this->irq_pin_);
  ESP_LOGCONFIG(TAG, "  Radio: %s", radio_ok_ ? "nRF24 connected" : "not connected");
  ESP_LOGCONFIG(TAG, "  STATUS=0x%02X CONFIG=0x%02X SETUP_AW=0x%02X RF_SETUP=0x%02X", detected_status_,
                detected_config_, detected_setup_aw_, detected_rf_setup_);
  ESP_LOGCONFIG(TAG, "  Poll interval: %ums", poll_interval_ms_);
  ESP_LOGCONFIG(TAG, "  DTU serial: 0x%08X", dtu_serial_);
  for (auto *inverter : inverters_) {
    ESP_LOGCONFIG(TAG, "  Inverter serial=%012llu model=%s status=%s last_seen=%us error=%s", inverter->get_serial(),
                  hm_model_to_string(inverter->get_model()), hm_status_to_string(inverter->get_status()),
                  inverter->get_last_seen() / 1000, inverter->get_last_error());
  }
}

bool HoymilesDtuComponent::setup_radio_() {
  delay(20);
  detected_status_ = command_(NRF_NOP);
  detected_config_ = read_register_(NRF_REG_CONFIG);
  detected_setup_aw_ = read_register_(NRF_REG_SETUP_AW);
  const uint8_t restore_setup_aw = detected_setup_aw_;
  write_register_(NRF_REG_SETUP_AW, 0x03);
  detected_setup_aw_ = read_register_(NRF_REG_SETUP_AW);
  write_register_(NRF_REG_SETUP_AW, restore_setup_aw);

  if (!plausible_status_(detected_status_) || detected_setup_aw_ != 0x03) {
    return false;
  }
  configure_radio_();
  detected_config_ = read_register_(NRF_REG_CONFIG);
  detected_rf_setup_ = read_register_(NRF_REG_RF_SETUP);
  return true;
}

void HoymilesDtuComponent::configure_radio_() {
  set_ce_(false);
  write_register_(NRF_REG_CONFIG, NRF_CONFIG_EN_CRC | NRF_CONFIG_CRCO | NRF_CONFIG_PWR_UP);
  write_register_(NRF_REG_EN_AA, 0x3F);
  write_register_(NRF_REG_EN_RXADDR, 0x03);
  write_register_(NRF_REG_SETUP_AW, 0x03);
  write_register_(NRF_REG_SETUP_RETR, 0x3F);
  write_register_(NRF_REG_RF_SETUP, 0x20 | (static_cast<uint8_t>(pa_level_) << 1));
  write_register_(NRF_REG_DYNPD, 0x3F);
  write_register_(NRF_REG_FEATURE, 0x04);
  if ((read_register_(NRF_REG_FEATURE) & 0x04) == 0) {
    const uint8_t activate = 0x73;
    command_(NRF_ACTIVATE, &activate, nullptr, 1);
    write_register_(NRF_REG_FEATURE, 0x04);
    write_register_(NRF_REG_DYNPD, 0x3F);
  }
  command_(NRF_FLUSH_RX);
  command_(NRF_FLUSH_TX);
  clear_status_(NRF_STATUS_RX_DR | NRF_STATUS_TX_DS | NRF_STATUS_MAX_RT);
  delay(5);
}

void HoymilesDtuComponent::start_request_(HoymilesDtuInverter *inverter, uint32_t now) {
  uint8_t request[32];
  const uint8_t len = hm_build_realtime_request(inverter->get_radio_id(), dtu_serial_, now / 1000, request, sizeof(request));
  if (len == 0) {
    inverter->set_last_error("request build failed");
    return;
  }
  active_inverter_ = inverter;
  active_inverter_->mark_poll_started(now);
  frame_count_ = 0;
  tx_channel_index_ = (tx_channel_index_ + 1) % 5;
  rx_channel_index_ = (tx_channel_index_ + 3) % 5;
  request_started_ms_ = now;
  last_channel_switch_ms_ = now;
  stop_listening_();
  set_channel_(HM_RF_CHANNELS[tx_channel_index_]);
  uint8_t inverter_address[5];
  hm_radio_id_to_address(inverter->get_radio_id(), inverter_address);
  write_register_(NRF_REG_TX_ADDR, inverter_address, sizeof(inverter_address));
  write_register_(NRF_REG_RX_ADDR_P0, inverter_address, sizeof(inverter_address));
  request_state_ = tx_payload_(request, len) ? RequestState::WAIT_TX : RequestState::WAIT_RX;
  if (request_state_ == RequestState::WAIT_RX) {
    start_listening_(HM_RF_CHANNELS[rx_channel_index_]);
  }
}

void HoymilesDtuComponent::poll_tx_(uint32_t now) {
  const uint8_t status = read_register_(NRF_REG_STATUS);
  if ((status & NRF_STATUS_TX_DS) != 0) {
    clear_status_(NRF_STATUS_TX_DS);
    start_listening_(HM_RF_CHANNELS[rx_channel_index_]);
    request_state_ = RequestState::WAIT_RX;
    return;
  }
  if ((status & NRF_STATUS_MAX_RT) != 0) {
    clear_status_(NRF_STATUS_MAX_RT);
    command_(NRF_FLUSH_TX);
    start_listening_(HM_RF_CHANNELS[rx_channel_index_]);
    request_state_ = RequestState::WAIT_RX;
    return;
  }
  if (now - request_started_ms_ > TX_TIMEOUT_MS) {
    start_listening_(HM_RF_CHANNELS[rx_channel_index_]);
    request_state_ = RequestState::WAIT_RX;
  }
}

void HoymilesDtuComponent::poll_rx_(uint32_t now) {
  read_available_packets_();
  if (process_response_(now)) {
    finish_request_(true, "", now);
    return;
  }
  if (now - last_channel_switch_ms_ >= RX_CHANNEL_DWELL_MS) {
    rx_channel_index_ = (rx_channel_index_ + 4) % 5;
    set_channel_(HM_RF_CHANNELS[rx_channel_index_]);
    last_channel_switch_ms_ = now;
  }
  if (now - request_started_ms_ >= RX_TIMEOUT_MS) {
    finish_request_(false, frame_count_ == 0 ? "no response" : "incomplete response", now);
  }
}

void HoymilesDtuComponent::finish_request_(bool success, const char *error, uint32_t now) {
  if (!success && active_inverter_ != nullptr) {
    active_inverter_->set_last_error(error);
    ESP_LOGW(TAG, "Inverter %012llu poll failed: %s", active_inverter_->get_serial(), error);
  }
  stop_listening_();
  active_inverter_ = nullptr;
  request_state_ = RequestState::IDLE;
}

void HoymilesDtuComponent::read_available_packets_() {
  uint8_t status = read_register_(NRF_REG_STATUS);
  if ((status & NRF_STATUS_RX_DR) == 0 && (read_register_(NRF_REG_FIFO_STATUS) & NRF_FIFO_RX_EMPTY) != 0) {
    return;
  }
  while ((read_register_(NRF_REG_FIFO_STATUS) & NRF_FIFO_RX_EMPTY) == 0) {
    uint8_t packet[32];
    uint8_t len = 0;
    if (!rx_payload_(packet, &len)) {
      break;
    }
    if (active_inverter_ == nullptr || frame_count_ >= HM_MAX_FRAME_COUNT) {
      continue;
    }
    HmFrame frame;
    if (!hm_parse_frame(packet, len, active_inverter_->get_radio_id(), &frame)) {
      ESP_LOGV(TAG, "Ignored invalid HM frame len=%u", len);
      continue;
    }
    bool replaced = false;
    for (uint8_t i = 0; i < frame_count_; i++) {
      if (frames_[i].id == frame.id) {
        frames_[i] = frame;
        replaced = true;
        break;
      }
    }
    if (!replaced) {
      frames_[frame_count_++] = frame;
    }
  }
  clear_status_(NRF_STATUS_RX_DR);
}

bool HoymilesDtuComponent::process_response_(uint32_t now) {
  if (active_inverter_ == nullptr || frame_count_ < expected_frames_) {
    return false;
  }
  uint8_t payload[HM_MAX_PAYLOAD_SIZE];
  uint8_t payload_len = 0;
  if (!hm_assemble_payload(frames_.data(), expected_frames_, payload, sizeof(payload), &payload_len)) {
    return false;
  }
  HmTelemetry telemetry;
  if (!hm_parse_4ch_payload(payload, payload_len, &telemetry)) {
    active_inverter_->set_last_error("payload parse failed");
    return false;
  }
  active_inverter_->publish_telemetry(telemetry, now);
  return true;
}

void HoymilesDtuComponent::start_listening_(uint8_t channel) {
  command_(NRF_FLUSH_RX);
  clear_status_(NRF_STATUS_RX_DR | NRF_STATUS_TX_DS | NRF_STATUS_MAX_RT);
  set_channel_(channel);
  uint8_t dtu_address[5];
  hm_radio_id_to_address(hm_radio_id_from_serial(dtu_serial_), dtu_address);
  write_register_(NRF_REG_RX_ADDR_P1, dtu_address, sizeof(dtu_address));
  set_rx_mode_(true);
  set_ce_(true);
  delayMicroseconds(130);
}

void HoymilesDtuComponent::stop_listening_() {
  set_ce_(false);
  set_rx_mode_(false);
  delayMicroseconds(130);
}

void HoymilesDtuComponent::set_channel_(uint8_t channel) { write_register_(NRF_REG_RF_CH, channel); }

void HoymilesDtuComponent::set_rx_mode_(bool enabled) {
  uint8_t config = NRF_CONFIG_EN_CRC | NRF_CONFIG_CRCO | NRF_CONFIG_PWR_UP;
  if (enabled) {
    config |= NRF_CONFIG_PRIM_RX;
  }
  write_register_(NRF_REG_CONFIG, config);
}

void HoymilesDtuComponent::set_ce_(bool level) {
  if (ce_pin_ != nullptr) {
    ce_pin_->digital_write(level);
  }
}

void HoymilesDtuComponent::clear_status_(uint8_t mask) { write_register_(NRF_REG_STATUS, mask); }

bool HoymilesDtuComponent::tx_payload_(const uint8_t *payload, uint8_t len) {
  command_(NRF_FLUSH_TX);
  clear_status_(NRF_STATUS_RX_DR | NRF_STATUS_TX_DS | NRF_STATUS_MAX_RT);
  set_rx_mode_(false);
  command_(NRF_W_TX_PAYLOAD, payload, nullptr, len);
  set_ce_(true);
  delayMicroseconds(15);
  set_ce_(false);
  return true;
}

bool HoymilesDtuComponent::rx_payload_(uint8_t *payload, uint8_t *len) {
  const uint8_t width = get_dynamic_payload_size_();
  if (width == 0 || width > 32) {
    command_(NRF_FLUSH_RX);
    return false;
  }
  memset(payload, 0, 32);
  command_(NRF_R_RX_PAYLOAD, nullptr, payload, width);
  *len = width;
  return true;
}

uint8_t HoymilesDtuComponent::get_dynamic_payload_size_() {
  uint8_t width = 0;
  command_(NRF_R_RX_PL_WID, nullptr, &width, 1);
  return width;
}

uint8_t HoymilesDtuComponent::read_register_(uint8_t reg) {
  uint8_t value = 0xFF;
  read_register_(reg, &value, 1);
  return value;
}

void HoymilesDtuComponent::read_register_(uint8_t reg, uint8_t *data, uint8_t len) {
  command_(NRF_R_REGISTER | (reg & 0x1F), nullptr, data, len);
}

void HoymilesDtuComponent::write_register_(uint8_t reg, uint8_t value) { write_register_(reg, &value, 1); }

void HoymilesDtuComponent::write_register_(uint8_t reg, const uint8_t *data, uint8_t len) {
  command_(NRF_W_REGISTER | (reg & 0x1F), data, nullptr, len);
}

uint8_t HoymilesDtuComponent::command_(uint8_t command) { return command_(command, nullptr, nullptr, 0); }

uint8_t HoymilesDtuComponent::command_(uint8_t command, const uint8_t *tx, uint8_t *rx, uint8_t len) {
  enable();
  const uint8_t status = transfer_byte(command);
  for (uint8_t i = 0; i < len; i++) {
    const uint8_t tx_byte = tx == nullptr ? NRF_NOP : tx[i];
    const uint8_t rx_byte = transfer_byte(tx_byte);
    if (rx != nullptr) {
      rx[i] = rx_byte;
    }
  }
  disable();
  return status;
}

bool HoymilesDtuComponent::plausible_status_(uint8_t status) {
  return status != 0x00 && status != 0xFF && (status & 0x80) == 0;
}

}  // namespace hoymiles_dtu
}  // namespace esphome
