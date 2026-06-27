#pragma once

#include "protocol.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"

#include <array>
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
  void set_irq_pin(GPIOPin *pin) { irq_pin_ = pin; }
  void set_poll_interval(uint32_t poll_interval) { poll_interval_ms_ = poll_interval; }
  void set_pa_level(HmPaLevel pa_level) { pa_level_ = pa_level; }
  void set_dtu_serial(uint32_t dtu_serial) {
    dtu_serial_ = dtu_serial;
    dtu_serial_configured_ = true;
  }
  void add_inverter(HoymilesDtuInverter *inverter) { inverters_.push_back(inverter); }

  void setup() override;
  void loop() override;
  void dump_config() override;

 protected:
  enum class RequestState : uint8_t {
    IDLE,
    WAIT_TX,
    WAIT_RX,
  };

  bool setup_radio_();
  void configure_radio_();
  void start_request_(HoymilesDtuInverter *inverter, uint32_t now);
  void poll_tx_(uint32_t now);
  void poll_rx_(uint32_t now);
  void finish_request_(bool success, const char *error, uint32_t now);
  void read_available_packets_();
  bool process_response_(uint32_t now);
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
  GPIOPin *irq_pin_{nullptr};
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
  uint32_t last_channel_switch_ms_{0};
  uint8_t tx_channel_index_{0};
  uint8_t rx_channel_index_{0};
  uint8_t expected_frames_{7};
  uint8_t frame_count_{0};
  std::array<HmFrame, HM_MAX_FRAME_COUNT> frames_{};
};

}  // namespace hoymiles_dtu
}  // namespace esphome
