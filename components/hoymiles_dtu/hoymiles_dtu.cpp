#include "hoymiles_dtu.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <cstdio>
#include <cstring>
#include <ctime>

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

static constexpr uint32_t TX_TIMEOUT_MS = 100;
static constexpr uint32_t RX_TIMEOUT_MS = 520;
static constexpr uint32_t RX_INITIAL_PENDULUM_MS = 85;
static constexpr uint32_t RX_CHANNEL_DWELL_MS = 5;
// Acquisition burst: on no-ACK we retransmit immediately on the next channel (non-blocking, one
// attempt per loop iteration) up to this many times before giving up for this poll. ~MAX/5 full
// channel sweeps; with a tight update_interval this approximates Ahoy's steady retransmit cadence.
static constexpr uint8_t MAX_TX_ATTEMPTS = 30;
// On a partial record (link is up, some fragments missing), re-request the whole record this many
// times before failing the exchange. Each re-request keeps already-received fragments.
static constexpr uint8_t MAX_RECORD_ATTEMPTS = 4;

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

bool HoymilesDtuInverter::due(uint32_t now, uint32_t update_interval) const {
  return last_poll_ms_ == 0 || now - last_poll_ms_ >= update_interval;
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
  if (reactive_power_sensor_ != nullptr) {
    reactive_power_sensor_->publish_state(telemetry.reactive_power);
  }
  if (power_factor_sensor_ != nullptr) {
    power_factor_sensor_->publish_state(telemetry.power_factor);
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
  const uint8_t channel_count = hm_model_channel_count(model_);
  for (uint8_t i = 0; i < HM_MAX_CHANNELS; i++) {
    const bool in_range = i < channel_count;
    const auto &data = telemetry.channels[i];
    const auto &sensors = channels_[i];
    if (sensors.dc_voltage != nullptr) {
      sensors.dc_voltage->publish_state(in_range ? data.dc_voltage : NAN);
    }
    if (sensors.dc_current != nullptr) {
      sensors.dc_current->publish_state(in_range ? data.dc_current : NAN);
    }
    if (sensors.dc_power != nullptr) {
      sensors.dc_power->publish_state(in_range ? data.dc_power : NAN);
    }
    if (sensors.yield_today != nullptr) {
      sensors.yield_today->publish_state(in_range ? data.yield_today : NAN);
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
    irq_pin_->attach_interrupt(&HoymilesDtuComponent::irq_handler_, this, gpio::INTERRUPT_FALLING_EDGE);
    irq_attached_ = true;
  } else if (irq_pin_base_ != nullptr) {
    irq_pin_base_->setup();
    ESP_LOGW(TAG, "IRQ pin is not an internal GPIO; using high-frequency polling fallback during exchanges");
  } else {
    ESP_LOGW(TAG, "IRQ pin is not configured; using high-frequency polling fallback during exchanges");
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

void HoymilesDtuComponent::update() {
  if (!radio_ok_ || request_state_ != RequestState::IDLE) {
    return;
  }
  const uint32_t now = millis();
  for (auto *inverter : inverters_) {
    if (inverter->due(now, this->get_update_interval())) {
      start_request_(inverter, now);
      break;
    }
  }
}

void HoymilesDtuComponent::loop() {
  if (!radio_ok_) {
    return;
  }
  const uint32_t now = millis();
  for (auto *inverter : inverters_) {
    inverter->mark_offline_if_expired(now, this->get_update_interval() * 4);
  }
  consume_irq_();

  switch (request_state_) {
    case RequestState::IDLE:
      if (power_limit_pending_) {
        power_limit_pending_ = false;
        start_power_limit_request_(now);
      }
      break;
    case RequestState::WAIT_TX_IRQ:
      poll_tx_(now);
      break;
    case RequestState::RX_ACTIVE:
      poll_rx_(now);
      break;
  }
}

void HoymilesDtuComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Hoymiles DTU:");
  LOG_PIN("  CS Pin: ", this->cs_);
  LOG_PIN("  CE Pin: ", this->ce_pin_);
  LOG_PIN("  IRQ Pin: ", this->irq_pin_base_);
  ESP_LOGCONFIG(TAG, "  IRQ mode: %s", irq_attached_ ? "falling-edge interrupt" : "high-frequency polling fallback");
  ESP_LOGCONFIG(TAG, "  Radio: %s", radio_ok_ ? "nRF24 connected" : "not connected");
  ESP_LOGCONFIG(TAG, "  STATUS=0x%02X CONFIG=0x%02X SETUP_AW=0x%02X RF_SETUP=0x%02X", detected_status_,
                detected_config_, detected_setup_aw_, detected_rf_setup_);
  LOG_UPDATE_INTERVAL(this);
  ESP_LOGCONFIG(TAG, "  DTU serial: 0x%08X", dtu_serial_);
  for (auto *inverter : inverters_) {
    ESP_LOGCONFIG(TAG, "  Inverter serial=%012llX model=%s status=%s last_seen=%us error=%s", inverter->get_serial(),
                  hm_model_to_string(inverter->get_model()), hm_status_to_string(inverter->get_status()),
                  inverter->get_last_seen() / 1000, inverter->get_last_error());
  }
}

void HoymilesDtuComponent::radio_set_power_limit(uint16_t percent, bool persistent) {
  if (!radio_ok_) {
    publish_radio_error_("radio not ready");
    return;
  }
  if (inverters_.empty()) {
    publish_radio_error_("no inverter configured");
    return;
  }
  power_limit_percent_ = percent;
  power_limit_persistent_ = persistent;
  power_limit_pending_ = true;
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

void HoymilesDtuComponent::begin_exchange_() { high_freq_.start(); }

void HoymilesDtuComponent::end_exchange_() { high_freq_.stop(); }

void HoymilesDtuComponent::consume_irq_() {
  // The nRF IRQ only wakes the loop promptly; poll_tx_/poll_rx_ read STATUS directly, so there is no
  // per-IRQ work to do here beyond clearing the ISR flag.
  irq_pending_ = false;
}

void IRAM_ATTR HoymilesDtuComponent::irq_handler_(HoymilesDtuComponent *arg) {
  arg->irq_pending_ = true;
  arg->enable_loop_soon_any_context();
}

void HoymilesDtuComponent::start_request_(HoymilesDtuInverter *inverter, uint32_t now) {
  active_inverter_ = inverter;
  active_inverter_->mark_poll_started(now);
  exchange_kind_ = ExchangeKind::TELEMETRY;
  frame_count_ = 0;
  // Unknown until the inverter sends the fragment with the 0x80 "last" bit; until then never treat
  // the record as complete. (read_available_packets_ lowers this to the real count.)
  expected_frames_ = HM_MAX_FRAME_COUNT;
  record_attempt_ = 0;
  rx_packet_count_ = 0;
  invalid_frame_count_ = 0;
  duplicate_frame_count_ = 0;
  tx_status_ = 0;
  tx_result_ = "no ack";
  irq_pending_ = false;
  tx_attempt_ = 0;
  begin_exchange_();

  // The inverter TX/ACK address is stable for the whole exchange; program it once.
  uint8_t inverter_address[5];
  hm_radio_id_to_address(inverter->get_radio_id(), inverter_address);
  write_register_(NRF_REG_TX_ADDR, inverter_address, sizeof(inverter_address));
  write_register_(NRF_REG_RX_ADDR_P0, inverter_address, sizeof(inverter_address));

  uint32_t request_timestamp = now / 1000;
  const std::time_t epoch = std::time(nullptr);
  if (epoch > 1600000000) {
    request_timestamp = static_cast<uint32_t>(epoch);
  }
  request_len_ =
      hm_build_realtime_request(inverter->get_radio_id(), dtu_serial_, request_timestamp, request_, sizeof(request_));
  if (request_len_ == 0) {
    finish_request_(false, "request build failed", now);
    return;
  }

  transmit_request_(now);
}

void HoymilesDtuComponent::start_power_limit_request_(uint32_t now) {
  HoymilesDtuInverter *inverter = inverters_.front();
  active_inverter_ = inverter;
  exchange_kind_ = ExchangeKind::DEV_CONTROL;
  frame_count_ = 0;
  expected_frames_ = HM_MAX_FRAME_COUNT;
  record_attempt_ = 0;
  rx_packet_count_ = 0;
  invalid_frame_count_ = 0;
  duplicate_frame_count_ = 0;
  tx_status_ = 0;
  tx_result_ = "no ack";
  irq_pending_ = false;
  tx_attempt_ = 0;
  dev_ctrl_got_response_ = false;
  dev_ctrl_confirmed_ = false;
  begin_exchange_();

  uint8_t inverter_address[5];
  hm_radio_id_to_address(inverter->get_radio_id(), inverter_address);
  write_register_(NRF_REG_TX_ADDR, inverter_address, sizeof(inverter_address));
  write_register_(NRF_REG_RX_ADDR_P0, inverter_address, sizeof(inverter_address));

  request_len_ = hm_build_power_limit_request(inverter->get_radio_id(), dtu_serial_, power_limit_percent_,
                                              power_limit_persistent_, request_, sizeof(request_));
  if (request_len_ == 0) {
    finish_request_(false, "power-limit build failed", now);
    return;
  }

  transmit_request_(now);
}

// Build and transmit one acquisition request. Called for the first attempt and for each
// burst retransmit. Mirrors Ahoy's acquisition: advance the TX channel each attempt (cold-start
// equivalent of its per-channel quality heuristic), transmit with auto-ack, and wait for the IRQ.
// The request payload is built once per exchange (start_request_/start_power_limit_request_) and
// reused for every retransmit in the burst, same as Ahoy keeps one payload for a whole burst.
void HoymilesDtuComponent::transmit_request_(uint32_t now) {
  if (active_inverter_ == nullptr) {
    finish_request_(false, "no inverter", now);
    return;
  }

  // Round-robin {3,23,40,61,75}. RX reply lands rxOffset=3 indices away (HM).
  tx_channel_index_ = (tx_channel_index_ + 1) % 5;
  rx_channel_index_ = (tx_channel_index_ + 3) % 5;

  request_started_ms_ = now;
  stop_listening_();
  set_channel_(HM_RF_CHANNELS[tx_channel_index_]);

#ifdef ESPHOME_LOG_HAS_DEBUG
  if (tx_attempt_ == 0) {
    uint8_t inverter_address[5];
    hm_radio_id_to_address(active_inverter_->get_radio_id(), inverter_address);
    ESP_LOGD(TAG, "TX request inverter=%012llX ch=%u addr=%s len=%u data=%s", active_inverter_->get_serial(),
             HM_RF_CHANNELS[tx_channel_index_], format_hex_pretty(inverter_address, sizeof(inverter_address)).c_str(),
             request_len_, format_hex_pretty(request_, request_len_).c_str());
  }
#endif

  tx_result_ = "no ack";
  if (tx_payload_(request_, request_len_)) {
    request_state_ = RequestState::WAIT_TX_IRQ;
  } else {
    begin_rx_window_(now);  // can't wait for a TX IRQ; fall back to listening
  }
}

void HoymilesDtuComponent::poll_tx_(uint32_t now) {
  const uint8_t status = read_register_(NRF_REG_STATUS);
  if ((status & NRF_STATUS_TX_DS) != 0) {
    // ACK from the inverter: it received the request and will stream data frames. Listen now.
    set_ce_(false);
    tx_status_ = status;
    tx_result_ = "TX_DS";
    clear_status_(NRF_STATUS_TX_DS);
    ESP_LOGD(TAG, "TX_DS ch=%u attempt=%u", HM_RF_CHANNELS[tx_channel_index_], tx_attempt_);
    begin_rx_window_(now);
    return;
  }

  const bool max_rt = (status & NRF_STATUS_MAX_RT) != 0;
  const bool timed_out = now - request_started_ms_ > TX_TIMEOUT_MS;
  if (max_rt || timed_out) {
    // No ACK on this channel. Ahoy-style acquisition: immediately retransmit on the next channel
    // rather than waiting a whole poll interval, so we sweep the inverter's listen window fast.
    set_ce_(false);
    tx_status_ = status;
    tx_result_ = max_rt ? "MAX_RT" : "timeout";
    clear_status_(NRF_STATUS_MAX_RT);
    command_(NRF_FLUSH_TX);
    if (++tx_attempt_ < MAX_TX_ATTEMPTS) {
      transmit_request_(now);
    } else {
      finish_request_(false, "no ack", now);
    }
  }
}

void HoymilesDtuComponent::poll_rx_(uint32_t now) {
  read_available_packets_();

  if (exchange_kind_ == ExchangeKind::DEV_CONTROL) {
    // Single confirmation frame (0xD1): no re-request, no channel hopping — just wait out the
    // window on the parked channel (begin_rx_window_ already put us on tx_channel+3).
    if (dev_ctrl_confirmed_) {
      finish_request_(true, "", now);
      return;
    }
    if (now - rx_started_ms_ >= RX_TIMEOUT_MS) {
      finish_request_(false, dev_ctrl_got_response_ ? "power-limit rejected" : "no confirmation", now);
    }
    return;
  }

  if (process_response_(now)) {
    finish_request_(true, "", now);
    return;
  }
  if (now - rx_started_ms_ >= RX_TIMEOUT_MS) {
    // Partial record: the inverter is answering but we missed fragment(s). Re-request the whole
    // record (keeping fragments we already have — read_available_packets_ dedups by id) up to a
    // small budget, instead of giving up. Mirrors Ahoy's missing-fragment retransmit.
    if (frame_count_ > 0 && frame_count_ < expected_frames_ && ++record_attempt_ <= MAX_RECORD_ATTEMPTS) {
      ESP_LOGD(TAG, "Incomplete record %u/%u - re-requesting (attempt %u)", frame_count_, expected_frames_,
               record_attempt_);
      tx_attempt_ = 0;  // give the re-request a fresh acquisition burst
      transmit_request_(now);
      return;
    }
    finish_request_(false, frame_count_ == 0 ? "no response" : "incomplete response", now);
    return;
  }

  if (now - last_channel_switch_ms_ < RX_CHANNEL_DWELL_MS) {
    return;
  }

  if (!rx_loop_channels_ && now - rx_started_ms_ > RX_INITIAL_PENDULUM_MS) {
    rx_loop_channels_ = true;
  }

  if (rx_loop_channels_) {
    current_rx_channel_index_ = (current_rx_channel_index_ + 4) % 5;
  } else {
    rx_pendular_ = !rx_pendular_;
    current_rx_channel_index_ = (rx_channel_index_ + (rx_pendular_ ? 4 : 0)) % 5;
  }
  set_channel_(HM_RF_CHANNELS[current_rx_channel_index_]);
  last_channel_switch_ms_ = now;
}

void HoymilesDtuComponent::finish_request_(bool success, const char *error, uint32_t now) {
  log_exchange_summary_(success, error, now);
  if (exchange_kind_ == ExchangeKind::DEV_CONTROL) {
    if (!success) {
      publish_radio_error_(error);
    }
  } else if (!success && active_inverter_ != nullptr) {
    active_inverter_->set_last_error(error);
  }
  stop_listening_();
  active_inverter_ = nullptr;
  request_state_ = RequestState::IDLE;
  end_exchange_();
}

void HoymilesDtuComponent::log_exchange_summary_(bool success, const char *error, uint32_t now) {
  const uint8_t tx_channel = HM_RF_CHANNELS[tx_channel_index_];
  if (exchange_kind_ == ExchangeKind::DEV_CONTROL) {
    const char *result =
        success ? "ACCEPTED by inverter" : (dev_ctrl_got_response_ ? "REJECTED" : "no confirmation frame");
    ESP_LOGI(TAG, "Power limit %u%% (%s): TX_DS on ch=%u -> %s", power_limit_percent_,
             power_limit_persistent_ ? "persistent" : "non-persistent", tx_channel, result);
    return;
  }

  const uint32_t duration_ms = now - request_started_ms_;
  const uint8_t rx_channel = HM_RF_CHANNELS[rx_channel_index_];
  const char *result = success ? "complete" : error;
  const uint64_t serial = active_inverter_ == nullptr ? 0 : active_inverter_->get_serial();
  if (success) {
    ESP_LOGI(TAG,
             "Exchange %012llX complete: tx_ch=%u tx=%s rx_start=%u packets=%u accepted=%u invalid=%u "
             "duplicate=%u duration=%ums",
             serial, tx_channel, tx_result_, rx_channel, rx_packet_count_, frame_count_, invalid_frame_count_,
             duplicate_frame_count_, duration_ms);
  } else {
    ESP_LOGW(TAG,
             "Exchange %012llX failed: reason=%s tx_ch=%u tx=%s status=0x%02X rx_start=%u packets=%u accepted=%u "
             "invalid=%u duplicate=%u duration=%ums",
             serial, result, tx_channel, tx_result_, tx_status_, rx_channel, rx_packet_count_, frame_count_,
             invalid_frame_count_, duplicate_frame_count_, duration_ms);
  }
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
    rx_packet_count_++;
#ifdef ESPHOME_LOG_HAS_DEBUG
    const std::string packet_hex = format_hex_pretty(packet, len);
    ESP_LOGD(TAG, "RX payload ch=%u len=%u data=%s", read_register_(NRF_REG_RF_CH), len, packet_hex.c_str());
    if (last_rx_payload_text_sensor_ != nullptr) {
      last_rx_payload_text_sensor_->publish_state(packet_hex);
    }
#else
    if (last_rx_payload_text_sensor_ != nullptr) {
      last_rx_payload_text_sensor_->publish_state(format_hex_pretty(packet, len));
    }
#endif
    if (active_inverter_ == nullptr) {
      invalid_frame_count_++;
      continue;
    }
    if (exchange_kind_ == ExchangeKind::DEV_CONTROL) {
      // DevControl confirmation (0xD1) doesn't fit the 0x95 realtime-frame shape hm_parse_frame
      // expects; check the raw layout directly (Ahoy parseDevCtrl: [10]==[11]==0 accepted).
      if (len >= 14 && packet[0] == (HM_TX_REQ_DEVCONTROL | HM_ALL_FRAMES) && packet[12] == HM_DEV_CTRL_ACTIVE_POWER) {
        dev_ctrl_got_response_ = true;
        dev_ctrl_confirmed_ = (packet[10] == 0x00 && packet[11] == 0x00 && packet[13] == 0x00);
      }
      continue;
    }
    if (frame_count_ >= HM_MAX_FRAME_COUNT) {
      invalid_frame_count_++;
      continue;
    }
    HmFrame frame;
    if (!hm_parse_frame(packet, len, active_inverter_->get_radio_id(), &frame)) {
      invalid_frame_count_++;
      ESP_LOGW(TAG, "Ignored invalid HM frame len=%u data=%s", len, format_hex_pretty(packet, len).c_str());
      continue;
    }
    bool replaced = false;
    for (uint8_t i = 0; i < frame_count_; i++) {
      if (frames_[i].id == frame.id) {
        frames_[i] = frame;
        replaced = true;
        duplicate_frame_count_++;
        break;
      }
    }
    if (!replaced) {
      frames_[frame_count_++] = frame;
      ESP_LOGD(TAG, "Accepted HM frame id=%u len=%u count=%u/%u", frame.id, frame.len, frame_count_, expected_frames_);
    }
    // The high bit of the fragment counter (packet[9] & 0x80) marks the final fragment; its id is
    // the total fragment count for this inverter's record (varies, e.g. 4 here, not a fixed 7).
    if ((packet[9] & 0x80) != 0) {
      expected_frames_ = frame.id;
    }
  }
  clear_status_(NRF_STATUS_RX_DR);
}

void HoymilesDtuComponent::publish_radio_error_(const char *error) {
  ESP_LOGW(TAG, "Radio error: %s", error);
  if (last_radio_error_text_sensor_ != nullptr) {
    last_radio_error_text_sensor_->publish_state(error);
  }
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
  if (!hm_parse_realtime_payload(active_inverter_->get_model(), payload, payload_len, &telemetry)) {
    active_inverter_->set_last_error("payload parse failed");
    return false;
  }
  float dc_power = 0.0f;
  for (uint8_t c = 0; c < hm_model_channel_count(active_inverter_->get_model()); c++) {
    dc_power += telemetry.channels[c].dc_power;
  }
  ESP_LOGI(TAG,
           "Telemetry %012llX: AC %.1fV %.2fHz %.1fW %.2fA %.1fvar pf=%.2f | DC %.1fW | %.1f°C | "
           "YieldToday %.0fWh YieldTotal %.3fkWh | evt=%u",
           active_inverter_->get_serial(), telemetry.ac_voltage, telemetry.ac_frequency, telemetry.ac_power,
           telemetry.ac_current, telemetry.reactive_power, telemetry.power_factor, dc_power, telemetry.temperature,
           telemetry.yield_today, telemetry.yield_total, telemetry.event_code);
  active_inverter_->publish_telemetry(telemetry, now);
  return true;
}

void HoymilesDtuComponent::begin_rx_window_(uint32_t now) {
  rx_started_ms_ = now;
  last_channel_switch_ms_ = now;
  current_rx_channel_index_ = rx_channel_index_;
  rx_loop_channels_ = false;
  rx_pendular_ = false;
  start_listening_(HM_RF_CHANNELS[current_rx_channel_index_]);
  request_state_ = RequestState::RX_ACTIVE;
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
  if (payload == nullptr || len == 0 || len > 32) {
    return false;
  }
  command_(NRF_FLUSH_TX);
  clear_status_(NRF_STATUS_RX_DR | NRF_STATUS_TX_DS | NRF_STATUS_MAX_RT);
  set_rx_mode_(false);
  command_(NRF_W_TX_PAYLOAD, payload, nullptr, len);
  ESP_LOGD(TAG, "TX loaded fifo=0x%02X status=0x%02X config=0x%02X", read_register_(NRF_REG_FIFO_STATUS),
           read_register_(NRF_REG_STATUS), read_register_(NRF_REG_CONFIG));
  set_ce_(true);
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
