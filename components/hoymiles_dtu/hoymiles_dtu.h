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

enum HmSerialFormat : uint8_t {
  SERIAL_DECIMAL = 0,
  SERIAL_BCD = 1,
  SERIAL_RAW = 2,
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

  bool due(uint32_t now, uint32_t poll_interval) const;
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
  sensor::Sensor *temperature_sensor_{nullptr};
  sensor::Sensor *yield_today_sensor_{nullptr};
  sensor::Sensor *yield_total_sensor_{nullptr};
  text_sensor::TextSensor *status_text_sensor_{nullptr};
  text_sensor::TextSensor *last_seen_text_sensor_{nullptr};
  std::array<HmChannelSensors, HM_MAX_CHANNELS> channels_{};
};

class HoymilesDtuComponent : public Component,
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
  void set_poll_interval(uint32_t poll_interval) { poll_interval_ms_ = poll_interval; }
  void set_pa_level(HmPaLevel pa_level) { pa_level_ = pa_level; }
  void set_dtu_serial(uint32_t dtu_serial) {
    dtu_serial_ = dtu_serial;
    dtu_serial_configured_ = true;
  }
  void add_inverter(HoymilesDtuInverter *inverter) { inverters_.push_back(inverter); }
  void set_last_tx_text_sensor(text_sensor::TextSensor *sensor) { last_tx_text_sensor_ = sensor; }
  void set_last_rx_payload_text_sensor(text_sensor::TextSensor *sensor) { last_rx_payload_text_sensor_ = sensor; }
  void set_last_register_dump_text_sensor(text_sensor::TextSensor *sensor) { last_register_dump_text_sensor_ = sensor; }
  void set_last_radio_error_text_sensor(text_sensor::TextSensor *sensor) { last_radio_error_text_sensor_ = sensor; }

  void radio_dump();
  void radio_send_request(const std::string &serial, HmSerialFormat serial_format, uint8_t tx_channel,
                          int8_t rx_offset, uint16_t rx_window_ms, uint16_t rx_dwell_ms, HmPaLevel pa_level,
                          uint32_t timestamp);
  void radio_send_raw(const std::string &address_hex, uint8_t tx_channel, const std::string &payload_hex,
                      int8_t rx_offset, uint16_t rx_window_ms, uint16_t rx_dwell_ms, HmPaLevel pa_level);
  void radio_listen(uint8_t channel, uint16_t window_ms, uint16_t dwell_ms);

  void setup() override;
  void loop() override;
  void dump_config() override;

 protected:
  enum class RequestState : uint8_t {
    IDLE,
    WAIT_TX_IRQ,
    RX_ACTIVE,
  };

  bool setup_radio_();
  void configure_radio_();
  void start_request_(HoymilesDtuInverter *inverter, uint32_t now);
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
  void begin_rx_window_(uint32_t now);
  bool debug_can_run_();
  void debug_prepare_();
  void debug_restore_(HmPaLevel restore_pa_level);
  bool debug_send_payload_(const uint8_t *payload, uint8_t len, const uint8_t *address, uint8_t tx_channel,
                           HmPaLevel pa_level, uint8_t *final_status);
  void debug_listen_window_(uint8_t start_channel, int8_t rx_offset, uint16_t window_ms, uint16_t dwell_ms);
  void debug_publish_last_tx_(uint8_t channel, const uint8_t *address, uint8_t status);
  void debug_publish_register_dump_();
  void debug_publish_error_(const char *error);
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
  static bool parse_hex_(const std::string &hex, uint8_t *buffer, uint8_t max_len, uint8_t *out_len);
  static bool parse_serial_radio_id_(const std::string &serial, HmSerialFormat format, uint64_t *radio_id);
  static uint8_t channel_with_offset_(uint8_t tx_channel, int8_t rx_offset);

  GPIOPin *ce_pin_{nullptr};
  GPIOPin *irq_pin_base_{nullptr};
  InternalGPIOPin *irq_pin_{nullptr};
  std::vector<HoymilesDtuInverter *> inverters_;
  uint32_t poll_interval_ms_{15000};
  HmPaLevel pa_level_{PA_LOW};
  uint32_t dtu_serial_{0};
  bool dtu_serial_configured_{false};
  bool radio_ok_{false};
  uint8_t detected_status_{0xFF};
  uint8_t detected_config_{0xFF};
  uint8_t detected_setup_aw_{0xFF};
  uint8_t detected_rf_setup_{0xFF};
  RequestState request_state_{RequestState::IDLE};
  HoymilesDtuInverter *active_inverter_{nullptr};
  uint32_t request_started_ms_{0};
  uint32_t rx_started_ms_{0};
  uint32_t last_channel_switch_ms_{0};
  uint8_t tx_channel_index_{0};
  uint8_t tx_attempt_{0};  // acquisition burst attempt counter (0-based) within one poll
  uint8_t rx_channel_index_{0};
  uint8_t current_rx_channel_index_{0};
  bool rx_loop_channels_{false};
  bool rx_pendular_{false};
  uint8_t expected_frames_{7};
  uint8_t frame_count_{0};
  uint8_t rx_packet_count_{0};
  uint8_t invalid_frame_count_{0};
  uint8_t duplicate_frame_count_{0};
  uint8_t tx_status_{0};
  uint8_t tx_observe_{0};
  uint8_t tx_fifo_status_{0};
  const char *tx_result_{"not sent"};
  volatile bool irq_pending_{false};
  bool irq_attached_{false};
  uint16_t irq_count_{0};
  uint16_t rpd_hits_{0};         // DIAG: poll_rx_ samples where nRF RPD (carrier >-64dBm) was high
  uint8_t rpd_channel_mask_{0};  // DIAG: bit i set if RPD was high while parked on channel index i
  HighFrequencyLoopRequester high_freq_;
  std::array<HmFrame, HM_MAX_FRAME_COUNT> frames_{};
  text_sensor::TextSensor *last_tx_text_sensor_{nullptr};
  text_sensor::TextSensor *last_rx_payload_text_sensor_{nullptr};
  text_sensor::TextSensor *last_register_dump_text_sensor_{nullptr};
  text_sensor::TextSensor *last_radio_error_text_sensor_{nullptr};
};

template<typename... Ts> class RadioDumpAction : public Action<Ts...>, public Parented<HoymilesDtuComponent> {
 public:
  void play(const Ts &...x) override { this->parent_->radio_dump(); }
};

template<typename... Ts> class RadioSendRequestAction : public Action<Ts...>, public Parented<HoymilesDtuComponent> {
 public:
  TEMPLATABLE_VALUE(std::string, serial)
  TEMPLATABLE_VALUE(uint8_t, serial_format)
  TEMPLATABLE_VALUE(uint8_t, tx_channel)
  TEMPLATABLE_VALUE(int8_t, rx_offset)
  TEMPLATABLE_VALUE(uint16_t, rx_window_ms)
  TEMPLATABLE_VALUE(uint16_t, rx_dwell_ms)
  TEMPLATABLE_VALUE(uint8_t, pa_level)
  TEMPLATABLE_VALUE(uint32_t, timestamp)

  void play(const Ts &...x) override {
    this->parent_->radio_send_request(this->serial_.value(x...), static_cast<HmSerialFormat>(this->serial_format_.value(x...)),
                                      this->tx_channel_.value(x...), this->rx_offset_.value(x...),
                                      this->rx_window_ms_.value(x...), this->rx_dwell_ms_.value(x...),
                                      static_cast<HmPaLevel>(this->pa_level_.value(x...)),
                                      this->timestamp_.value(x...));
  }
};

template<typename... Ts> class RadioSendRawAction : public Action<Ts...>, public Parented<HoymilesDtuComponent> {
 public:
  TEMPLATABLE_VALUE(std::string, address)
  TEMPLATABLE_VALUE(uint8_t, tx_channel)
  TEMPLATABLE_VALUE(std::string, payload)
  TEMPLATABLE_VALUE(int8_t, rx_offset)
  TEMPLATABLE_VALUE(uint16_t, rx_window_ms)
  TEMPLATABLE_VALUE(uint16_t, rx_dwell_ms)
  TEMPLATABLE_VALUE(uint8_t, pa_level)

  void play(const Ts &...x) override {
    this->parent_->radio_send_raw(this->address_.value(x...), this->tx_channel_.value(x...), this->payload_.value(x...),
                                  this->rx_offset_.value(x...), this->rx_window_ms_.value(x...),
                                  this->rx_dwell_ms_.value(x...), static_cast<HmPaLevel>(this->pa_level_.value(x...)));
  }
};

template<typename... Ts> class RadioListenAction : public Action<Ts...>, public Parented<HoymilesDtuComponent> {
 public:
  TEMPLATABLE_VALUE(uint8_t, channel)
  TEMPLATABLE_VALUE(uint16_t, window_ms)
  TEMPLATABLE_VALUE(uint16_t, dwell_ms)

  void play(const Ts &...x) override {
    this->parent_->radio_listen(this->channel_.value(x...), this->window_ms_.value(x...), this->dwell_ms_.value(x...));
  }
};

}  // namespace hoymiles_dtu
}  // namespace esphome
