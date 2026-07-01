#pragma once

#include "esphome/core/component.h"

#include <driver/gpio.h>
#include <driver/spi_master.h>

namespace esphome {
namespace nrf24_probe {

class Nrf24Probe : public Component {
 public:
  void set_pins(int ce_pin, int cs_pin, int sck_pin, int mosi_pin, int miso_pin, int irq_pin) {
    ce_pin_ = ce_pin;
    cs_pin_ = cs_pin;
    sck_pin_ = sck_pin;
    mosi_pin_ = mosi_pin;
    miso_pin_ = miso_pin;
    irq_pin_ = irq_pin;
  }

  void setup() override;
  void loop() override;
  void dump_config() override;

 private:
  struct TransferResult {
    uint8_t status{0xFF};
    uint8_t config{0xFF};
    uint8_t setup_aw_before{0xFF};
    uint8_t setup_aw_after{0xFF};
    uint8_t raw_nop{0xFF};
    uint8_t raw_config_status{0xFF};
    uint8_t raw_config_value{0xFF};
    bool writable_register_ok{false};
    bool plausible{false};
  };

  bool install_spi_device_(int clock_hz, int mode, bool hardware_cs);
  void remove_spi_device_();
  TransferResult run_transfer_probe_(bool manual_cs);
  void run_diagnostics_();
  TransferResult run_bitbang_probe_();
  uint8_t bitbang_transfer_byte_(uint8_t tx);
  void probe_output_pin_(int pin, int &low_level, int &high_level);
  int read_miso_with_pull_(gpio_pull_mode_t pull_mode);
  uint8_t read_register_(uint8_t reg);
  void write_register_(uint8_t reg, uint8_t value);
  bool transfer_(const uint8_t *tx, uint8_t *rx, size_t len, bool manual_cs = false);

  int ce_pin_{-1};
  int cs_pin_{-1};
  int sck_pin_{-1};
  int mosi_pin_{-1};
  int miso_pin_{-1};
  int irq_pin_{-1};
  spi_device_handle_t spi_{nullptr};
  bool spi_ok_{false};
  uint8_t status_{0xFF};
  uint8_t config_{0xFF};
  uint8_t en_aa_{0xFF};
  uint8_t setup_aw_before_{0xFF};
  uint8_t setup_aw_after_{0xFF};
  uint8_t rf_ch_{0xFF};
  uint8_t rf_setup_{0xFF};
  int irq_level_{-1};
  int ce_low_level_{-1};
  int ce_high_level_{-1};
  int cs_low_level_{-1};
  int cs_high_level_{-1};
  int sck_low_level_{-1};
  int sck_high_level_{-1};
  int mosi_low_level_{-1};
  int mosi_high_level_{-1};
  int miso_float_level_{-1};
  int miso_pullup_level_{-1};
  int miso_pulldown_level_{-1};
  uint8_t raw_nop_{0xFF};
  uint8_t raw_config_status_{0xFF};
  uint8_t raw_config_value_{0xFF};
  bool diagnostics_ran_{false};
  int64_t last_report_us_{0};
};

}  // namespace nrf24_probe
}  // namespace esphome
