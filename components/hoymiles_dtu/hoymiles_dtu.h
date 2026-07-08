#pragma once

#include "protocol.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/core/helpers.h"

#include <array>
#include <string>
#include <vector>

namespace esphome {
namespace hoymiles_dtu {

enum HmPaLevel : uint8_t {
  PA_MIN = 0,
  PA_LOW = 1,
  PA_HIGH = 2,
  PA_MAX = 3,
};

struct HmChannelSensors {
  sensor::Sensor *dc_voltage{nullptr};
  sensor::Sensor *dc_current{nullptr};
  sensor::Sensor *dc_power{nullptr};
  sensor::Sensor *yield_today{nullptr};
};

class HoymilesDtuInverter {
 public:
  void set_serial(uint64_t serial);
  void set_model(HmModel model) { model_ = model; }
  void set_ac_power_sensor(sensor::Sensor *sensor) { ac_power_sensor_ = sensor; }
  void set_ac_voltage_sensor(sensor::Sensor *sensor) { ac_voltage_sensor_ = sensor; }
  void set_ac_current_sensor(sensor::Sensor *sensor) { ac_current_sensor_ = sensor; }
  void set_ac_frequency_sensor(sensor::Sensor *sensor) { ac_frequency_sensor_ = sensor; }
  void set_reactive_power_sensor(sensor::Sensor *sensor) { reactive_power_sensor_ = sensor; }
  void set_power_factor_sensor(sensor::Sensor *sensor) { power_factor_sensor_ = sensor; }
  void set_temperature_sensor(sensor::Sensor *sensor) { temperature_sensor_ = sensor; }
  void set_yield_today_sensor(sensor::Sensor *sensor) { yield_today_sensor_ = sensor; }
  void set_yield_total_sensor(sensor::Sensor *sensor) { yield_total_sensor_ = sensor; }
  void set_status_text_sensor(text_sensor::TextSensor *sensor) { status_text_sensor_ = sensor; }
  void set_last_seen_text_sensor(text_sensor::TextSensor *sensor) { last_seen_text_sensor_ = sensor; }
  void set_channel_dc_voltage_sensor(uint8_t channel, sensor::Sensor *sensor);
  void set_channel_dc_current_sensor(uint8_t channel, sensor::Sensor *sensor);
  void set_channel_dc_power_sensor(uint8_t channel, sensor::Sensor *sensor);
  void set_channel_yield_today_sensor(uint8_t channel, sensor::Sensor *sensor);

  uint64_t get_serial() const { return serial_; }
  uint64_t get_radio_id() const { return radio_id_; }
  HmModel get_model() const { return model_; }
  HmStatus get_status() const { return status_; }
  uint32_t get_last_seen() const { return last_seen_ms_; }
  const char *get_last_error() const { return last_error_; }

  bool due(uint32_t now, uint32_t update_interval) const;
  void mark_poll_started(uint32_t now) { last_poll_ms_ = now; }
  void publish_telemetry(const HmTelemetry &telemetry, uint32_t now);
  void mark_offline_if_expired(uint32_t now, uint32_t timeout_ms);
  void set_last_error(const char *error) { last_error_ = error; }

 protected:
  void publish_status_();

  uint64_t serial_{0};
  uint64_t radio_id_{0};
  HmModel model_{HM_1500};
  HmStatus status_{OFFLINE};
  uint32_t last_poll_ms_{0};
  uint32_t last_seen_ms_{0};
  const char *last_error_{"never polled"};
  HmTelemetry telemetry_{};
  sensor::Sensor *ac_power_sensor_{nullptr};
  sensor::Sensor *ac_voltage_sensor_{nullptr};
  sensor::Sensor *ac_current_sensor_{nullptr};
  sensor::Sensor *ac_frequency_sensor_{nullptr};
  sensor::Sensor *reactive_power_sensor_{nullptr};
  sensor::Sensor *power_factor_sensor_{nullptr};
  sensor::Sensor *temperature_sensor_{nullptr};
  sensor::Sensor *yield_today_sensor_{nullptr};
  sensor::Sensor *yield_total_sensor_{nullptr};
  text_sensor::TextSensor *status_text_sensor_{nullptr};
  text_sensor::TextSensor *last_seen_text_sensor_{nullptr};
  std::array<HmChannelSensors, HM_MAX_CHANNELS> channels_{};
};

class HoymilesDtuComponent : public PollingComponent,
                             public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                                   spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_1MHZ> {
 public:
  void set_ce_pin(GPIOPin *pin) { ce_pin_ = pin; }
  void set_irq_pin(GPIOPin *pin) {
    irq_pin_base_ = pin;
    if (pin != nullptr && pin->is_internal()) {
      irq_pin_ = static_cast<InternalGPIOPin *>(pin);
    }
  }
  void set_pa_level(HmPaLevel pa_level) { pa_level_ = pa_level; }
  void set_scan_detection(bool enabled) { scan_detection_ = enabled; }
  void set_scan_detected_text_sensor(text_sensor::TextSensor *sensor) { scan_detected_text_sensor_ = sensor; }
  void set_dtu_serial(uint32_t dtu_serial) {
    dtu_serial_ = dtu_serial;
    dtu_serial_configured_ = true;
  }
  void add_inverter(HoymilesDtuInverter *inverter) { inverters_.push_back(inverter); }
  void set_last_rx_payload_text_sensor(text_sensor::TextSensor *sensor) { last_rx_payload_text_sensor_ = sensor; }
  void set_last_radio_error_text_sensor(text_sensor::TextSensor *sensor) { last_radio_error_text_sensor_ = sensor; }

  void radio_set_power_limit(uint16_t percent, bool persistent);

  void setup() override;
  void update() override;
  void loop() override;
  void dump_config() override;

 protected:
  enum class RequestState : uint8_t {
    IDLE,
    WAIT_TX_IRQ,
    RX_ACTIVE,
  };

  enum class ExchangeKind : uint8_t {
    TELEMETRY,
    DEV_CONTROL,
  };

  bool setup_radio_();
  void configure_radio_();
  void start_request_(HoymilesDtuInverter *inverter, uint32_t now);
  void start_power_limit_request_(uint32_t now);
  void transmit_request_(uint32_t now);
  void begin_exchange_();
  void end_exchange_();
  void consume_irq_();
  static void irq_handler_(HoymilesDtuComponent *arg);
  void poll_tx_(uint32_t now);
  void poll_rx_(uint32_t now);
  void finish_request_(bool success, const char *error, uint32_t now);
  void log_exchange_summary_(bool success, const char *error, uint32_t now);
  void read_available_packets_();
  bool process_response_(uint32_t now);
  // Passive scan detection: while idle, listen for foreign DTU requests to our inverter and for
  // Search-ID broadcasts, then report them. Receive-only — never transmits.
  void poll_monitor_(uint32_t now);
  void enter_monitor_(uint32_t now);
  void exit_monitor_();
  void read_monitor_packets_(uint32_t now);
  void report_scan_(const HmSniffResult &sniff, uint32_t now);
  void begin_rx_window_(uint32_t now);
  void publish_radio_error_(const char *error);
  void start_listening_(uint8_t channel);
  void stop_listening_();
  void set_channel_(uint8_t channel);
  void set_rx_mode_(bool enabled);
  void set_ce_(bool level);
  void clear_status_(uint8_t mask);
  bool tx_payload_(const uint8_t *payload, uint8_t len);
  bool rx_payload_(uint8_t *payload, uint8_t *len);
  uint8_t get_dynamic_payload_size_();
  uint8_t read_register_(uint8_t reg);
  void read_register_(uint8_t reg, uint8_t *data, uint8_t len);
  void write_register_(uint8_t reg, uint8_t value);
  void write_register_(uint8_t reg, const uint8_t *data, uint8_t len);
  uint8_t command_(uint8_t command);
  uint8_t command_(uint8_t command, const uint8_t *tx, uint8_t *rx, uint8_t len);
  static bool plausible_status_(uint8_t status);

  GPIOPin *ce_pin_{nullptr};
  GPIOPin *irq_pin_base_{nullptr};
  InternalGPIOPin *irq_pin_{nullptr};
  std::vector<HoymilesDtuInverter *> inverters_;
  HmPaLevel pa_level_{PA_LOW};
  uint32_t dtu_serial_{0};
  bool dtu_serial_configured_{false};
  bool radio_ok_{false};
  bool scan_detection_{false};
  bool monitoring_{false};
  uint8_t monitor_channel_index_{0};
  uint32_t monitor_last_hop_ms_{0};
  uint32_t scan_count_{0};
  uint32_t scan_last_report_ms_{0};
  uint8_t detected_status_{0xFF};
  uint8_t detected_config_{0xFF};
  uint8_t detected_setup_aw_{0xFF};
  uint8_t detected_rf_setup_{0xFF};
  RequestState request_state_{RequestState::IDLE};
  ExchangeKind exchange_kind_{ExchangeKind::TELEMETRY};
  HoymilesDtuInverter *active_inverter_{nullptr};
  uint32_t request_started_ms_{0};
  uint32_t rx_started_ms_{0};
  uint32_t last_channel_switch_ms_{0};
  uint8_t tx_channel_index_{0};
  uint8_t tx_attempt_{0};      // acquisition burst attempt counter (0-based) within one poll
  uint8_t record_attempt_{0};  // whole-record re-request counter when fragments are missing
  uint8_t rx_channel_index_{0};
  uint8_t current_rx_channel_index_{0};
  bool rx_loop_channels_{false};
  bool rx_pendular_{false};
  uint8_t expected_frames_{HM_MAX_FRAME_COUNT};
  uint8_t frame_count_{0};
  uint8_t rx_packet_count_{0};
  uint8_t invalid_frame_count_{0};
  uint8_t duplicate_frame_count_{0};
  uint8_t tx_status_{0};
  const char *tx_result_{"not sent"};
  uint8_t request_[32]{};
  uint8_t request_len_{0};
  bool power_limit_pending_{false};
  uint16_t power_limit_percent_{100};
  bool power_limit_persistent_{true};
  bool dev_ctrl_got_response_{false};
  bool dev_ctrl_confirmed_{false};
  volatile bool irq_pending_{false};
  bool irq_attached_{false};
  HighFrequencyLoopRequester high_freq_;
  std::array<HmFrame, HM_MAX_FRAME_COUNT> frames_{};
  text_sensor::TextSensor *last_rx_payload_text_sensor_{nullptr};
  text_sensor::TextSensor *last_radio_error_text_sensor_{nullptr};
  text_sensor::TextSensor *scan_detected_text_sensor_{nullptr};
};

template <typename... Ts>
class RadioSetPowerLimitAction : public Action<Ts...>, public Parented<HoymilesDtuComponent> {
 public:
  TEMPLATABLE_VALUE(uint16_t, percent)
  TEMPLATABLE_VALUE(bool, persistent)

  void play(const Ts &...x) override {
    this->parent_->radio_set_power_limit(this->percent_.value(x...), this->persistent_.value(x...));
  }
};

}  // namespace hoymiles_dtu
}  // namespace esphome
