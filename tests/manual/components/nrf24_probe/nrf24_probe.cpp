#include "nrf24_probe.h"

#include "esphome/core/log.h"

#include <esp_timer.h>

namespace esphome {
namespace nrf24_probe {

static const char *const TAG = "nrf24_probe";

static constexpr uint8_t R_REGISTER = 0x00;
static constexpr uint8_t W_REGISTER = 0x20;
static constexpr uint8_t NOP = 0xFF;
static constexpr uint8_t REG_CONFIG = 0x00;
static constexpr uint8_t REG_EN_AA = 0x01;
static constexpr uint8_t REG_SETUP_AW = 0x03;
static constexpr uint8_t REG_RF_CH = 0x05;
static constexpr uint8_t REG_RF_SETUP = 0x06;
static constexpr uint8_t REG_STATUS = 0x07;

static bool is_plausible_nrf24_status(uint8_t status) {
  return status != 0x00 && status != 0xFF && (status & 0x80) == 0;
}

void Nrf24Probe::setup() {
  ESP_LOGI(TAG, "Starting nRF24 wiring probe");
  ESP_LOGI(TAG, "Pins: CE=%d CS=%d SCK=%d MOSI=%d MISO=%d IRQ=%d", ce_pin_, cs_pin_, sck_pin_, mosi_pin_, miso_pin_,
           irq_pin_);

  gpio_reset_pin(static_cast<gpio_num_t>(ce_pin_));
  gpio_set_direction(static_cast<gpio_num_t>(ce_pin_), GPIO_MODE_OUTPUT);
  gpio_set_level(static_cast<gpio_num_t>(ce_pin_), 0);

  gpio_reset_pin(static_cast<gpio_num_t>(cs_pin_));
  gpio_set_direction(static_cast<gpio_num_t>(cs_pin_), GPIO_MODE_OUTPUT);
  gpio_set_level(static_cast<gpio_num_t>(cs_pin_), 1);

  probe_output_pin_(ce_pin_, ce_low_level_, ce_high_level_);
  probe_output_pin_(cs_pin_, cs_low_level_, cs_high_level_);
  probe_output_pin_(sck_pin_, sck_low_level_, sck_high_level_);
  probe_output_pin_(mosi_pin_, mosi_low_level_, mosi_high_level_);
  gpio_set_level(static_cast<gpio_num_t>(ce_pin_), 0);
  gpio_set_level(static_cast<gpio_num_t>(cs_pin_), 1);
  gpio_set_level(static_cast<gpio_num_t>(sck_pin_), 0);
  gpio_set_level(static_cast<gpio_num_t>(mosi_pin_), 0);

  gpio_reset_pin(static_cast<gpio_num_t>(miso_pin_));
  gpio_set_direction(static_cast<gpio_num_t>(miso_pin_), GPIO_MODE_INPUT);
  miso_float_level_ = read_miso_with_pull_(GPIO_FLOATING);
  miso_pullup_level_ = read_miso_with_pull_(GPIO_PULLUP_ONLY);
  miso_pulldown_level_ = read_miso_with_pull_(GPIO_PULLDOWN_ONLY);
  gpio_set_pull_mode(static_cast<gpio_num_t>(miso_pin_), GPIO_FLOATING);

  if (irq_pin_ >= 0) {
    gpio_reset_pin(static_cast<gpio_num_t>(irq_pin_));
    gpio_set_direction(static_cast<gpio_num_t>(irq_pin_), GPIO_MODE_INPUT);
    gpio_set_pull_mode(static_cast<gpio_num_t>(irq_pin_), GPIO_PULLUP_ONLY);
  }

  spi_bus_config_t bus_config = {};
  bus_config.mosi_io_num = mosi_pin_;
  bus_config.miso_io_num = miso_pin_;
  bus_config.sclk_io_num = sck_pin_;
  bus_config.quadwp_io_num = -1;
  bus_config.quadhd_io_num = -1;
  bus_config.max_transfer_sz = 8;

  spi_device_interface_config_t device_config = {};
  device_config.clock_speed_hz = 1000000;
  device_config.mode = 0;
  device_config.spics_io_num = cs_pin_;
  device_config.queue_size = 1;

  esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_DISABLED);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
    return;
  }

  if (!install_spi_device_(device_config.clock_speed_hz, device_config.mode, true)) {
    return;
  }

  delay(20);

  const uint8_t nop_tx[1] = {NOP};
  uint8_t nop_rx[1] = {0xFF};
  transfer_(nop_tx, nop_rx, sizeof(nop_tx));
  raw_nop_ = nop_rx[0];

  const uint8_t config_tx[2] = {REG_CONFIG, NOP};
  uint8_t config_rx[2] = {0xFF, 0xFF};
  transfer_(config_tx, config_rx, sizeof(config_tx));
  raw_config_status_ = config_rx[0];
  raw_config_value_ = config_rx[1];

  status_ = read_register_(REG_STATUS);
  config_ = read_register_(REG_CONFIG);
  en_aa_ = read_register_(REG_EN_AA);
  setup_aw_before_ = read_register_(REG_SETUP_AW);
  rf_ch_ = read_register_(REG_RF_CH);
  rf_setup_ = read_register_(REG_RF_SETUP);
  if (irq_pin_ >= 0) {
    irq_level_ = gpio_get_level(static_cast<gpio_num_t>(irq_pin_));
  }

  const uint8_t restored_setup_aw = setup_aw_before_;
  write_register_(REG_SETUP_AW, 0x03);
  setup_aw_after_ = read_register_(REG_SETUP_AW);
  write_register_(REG_SETUP_AW, restored_setup_aw);

  const bool miso_stuck = status_ == 0x00 || status_ == 0xFF;
  const bool writable_register_ok = setup_aw_after_ == 0x03;
  spi_ok_ = !miso_stuck && writable_register_ok;

  if (spi_ok_) {
    ESP_LOGI(TAG, "nRF24 SPI probe passed");
  } else {
    ESP_LOGE(TAG, "nRF24 SPI probe failed");
    if (miso_stuck) {
      ESP_LOGE(TAG, "STATUS is 0x%02X; MISO may be disconnected, swapped, or the radio may be unpowered", status_);
    }
    if (!writable_register_ok) {
      ESP_LOGE(TAG, "SETUP_AW write/read failed: wrote 0x03, read 0x%02X", setup_aw_after_);
    }
  }
}

void Nrf24Probe::loop() {
  const int64_t now = esp_timer_get_time();
  if (now - last_report_us_ < 5000000) {
    return;
  }
  last_report_us_ = now;
  if (!diagnostics_ran_) {
    run_diagnostics_();
    diagnostics_ran_ = true;
  }
  dump_config();
}

void Nrf24Probe::dump_config() {
  ESP_LOGCONFIG(TAG, "nRF24 Probe:");
  ESP_LOGCONFIG(TAG, "  CE: GPIO%d", ce_pin_);
  ESP_LOGCONFIG(TAG, "  CS: GPIO%d", cs_pin_);
  ESP_LOGCONFIG(TAG, "  SCK: GPIO%d", sck_pin_);
  ESP_LOGCONFIG(TAG, "  MOSI: GPIO%d", mosi_pin_);
  ESP_LOGCONFIG(TAG, "  MISO: GPIO%d", miso_pin_);
  ESP_LOGCONFIG(TAG, "  Output drive readback: CE=%d/%d CS=%d/%d SCK=%d/%d MOSI=%d/%d", ce_low_level_, ce_high_level_,
                cs_low_level_, cs_high_level_, sck_low_level_, sck_high_level_, mosi_low_level_, mosi_high_level_);
  ESP_LOGCONFIG(TAG, "  MISO idle levels: floating=%d pullup=%d pulldown=%d", miso_float_level_, miso_pullup_level_,
                miso_pulldown_level_);
  ESP_LOGCONFIG(TAG, "  IRQ: GPIO%d level=%d", irq_pin_, irq_level_);
  ESP_LOGCONFIG(TAG, "  Raw NOP STATUS=0x%02X Raw CONFIG read: status=0x%02X value=0x%02X", raw_nop_,
                raw_config_status_, raw_config_value_);
  ESP_LOGCONFIG(TAG, "  STATUS=0x%02X CONFIG=0x%02X EN_AA=0x%02X SETUP_AW(before)=0x%02X SETUP_AW(after)=0x%02X",
                status_, config_, en_aa_, setup_aw_before_, setup_aw_after_);
  ESP_LOGCONFIG(TAG, "  RF_CH=0x%02X RF_SETUP=0x%02X", rf_ch_, rf_setup_);
  ESP_LOGCONFIG(TAG, "  Result: %s", spi_ok_ ? "PASS" : "FAIL");
}

bool Nrf24Probe::install_spi_device_(int clock_hz, int mode, bool hardware_cs) {
  spi_device_interface_config_t device_config = {};
  device_config.clock_speed_hz = clock_hz;
  device_config.mode = mode;
  device_config.spics_io_num = hardware_cs ? cs_pin_ : -1;
  device_config.queue_size = 1;

  const esp_err_t err = spi_bus_add_device(SPI2_HOST, &device_config, &spi_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "spi_bus_add_device failed for %d Hz mode %d %s CS: %s", clock_hz, mode,
             hardware_cs ? "hardware" : "manual", esp_err_to_name(err));
    return false;
  }
  return true;
}

void Nrf24Probe::remove_spi_device_() {
  if (spi_ == nullptr) {
    return;
  }
  const esp_err_t err = spi_bus_remove_device(spi_);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "spi_bus_remove_device failed: %s", esp_err_to_name(err));
  }
  spi_ = nullptr;
}

Nrf24Probe::TransferResult Nrf24Probe::run_transfer_probe_(bool manual_cs) {
  TransferResult result;

  const uint8_t nop_tx[1] = {NOP};
  uint8_t nop_rx[1] = {0xFF};
  transfer_(nop_tx, nop_rx, sizeof(nop_tx), manual_cs);
  result.raw_nop = nop_rx[0];

  const uint8_t config_tx[2] = {REG_CONFIG, NOP};
  uint8_t config_rx[2] = {0xFF, 0xFF};
  transfer_(config_tx, config_rx, sizeof(config_tx), manual_cs);
  result.raw_config_status = config_rx[0];
  result.raw_config_value = config_rx[1];
  result.status = result.raw_nop;
  result.config = result.raw_config_value;

  const uint8_t setup_aw_tx[2] = {REG_SETUP_AW, NOP};
  uint8_t setup_aw_rx[2] = {0xFF, 0xFF};
  transfer_(setup_aw_tx, setup_aw_rx, sizeof(setup_aw_tx), manual_cs);
  result.setup_aw_before = setup_aw_rx[1];

  const uint8_t write_setup_aw_tx[2] = {static_cast<uint8_t>(W_REGISTER | REG_SETUP_AW), 0x03};
  uint8_t write_setup_aw_rx[2] = {0xFF, 0xFF};
  transfer_(write_setup_aw_tx, write_setup_aw_rx, sizeof(write_setup_aw_tx), manual_cs);

  uint8_t setup_aw_after_rx[2] = {0xFF, 0xFF};
  transfer_(setup_aw_tx, setup_aw_after_rx, sizeof(setup_aw_tx), manual_cs);
  result.setup_aw_after = setup_aw_after_rx[1];

  result.writable_register_ok = result.setup_aw_after == 0x03;
  result.plausible = is_plausible_nrf24_status(result.raw_nop) || is_plausible_nrf24_status(result.raw_config_status) ||
                     result.writable_register_ok;
  return result;
}

void Nrf24Probe::run_diagnostics_() {
  struct ProbeCase {
    int clock_hz;
    int mode;
    bool hardware_cs;
  };

  static constexpr ProbeCase CASES[] = {
      {1000000, 0, true}, {250000, 0, true},   {1000000, 1, true}, {1000000, 2, true},
      {1000000, 3, true}, {1000000, 0, false}, {250000, 0, false},
  };

  ESP_LOGI(TAG, "Running SPI diagnostic matrix");
  remove_spi_device_();
  for (const auto &probe_case : CASES) {
    gpio_set_level(static_cast<gpio_num_t>(cs_pin_), 1);
    if (!install_spi_device_(probe_case.clock_hz, probe_case.mode, probe_case.hardware_cs)) {
      continue;
    }
    delay(2);
    const TransferResult result = run_transfer_probe_(!probe_case.hardware_cs);
    ESP_LOGI(TAG,
             "case speed=%d mode=%d cs=%s raw_nop=0x%02X raw_config=(0x%02X,0x%02X) setup_aw=0x%02X->0x%02X "
             "write_ok=%s plausible=%s",
             probe_case.clock_hz, probe_case.mode, probe_case.hardware_cs ? "hardware" : "manual", result.raw_nop,
             result.raw_config_status, result.raw_config_value, result.setup_aw_before, result.setup_aw_after,
             result.writable_register_ok ? "yes" : "no", result.plausible ? "yes" : "no");
    remove_spi_device_();
  }

  remove_spi_device_();
  const esp_err_t free_err = spi_bus_free(SPI2_HOST);
  if (free_err != ESP_OK) {
    ESP_LOGW(TAG, "spi_bus_free before bitbang failed: %s", esp_err_to_name(free_err));
  }

  const TransferResult bitbang = run_bitbang_probe_();
  ESP_LOGI(TAG,
           "case gpio-bitbang mode=0 raw_nop=0x%02X raw_config=(0x%02X,0x%02X) setup_aw=0x%02X->0x%02X "
           "write_ok=%s plausible=%s",
           bitbang.raw_nop, bitbang.raw_config_status, bitbang.raw_config_value, bitbang.setup_aw_before,
           bitbang.setup_aw_after, bitbang.writable_register_ok ? "yes" : "no", bitbang.plausible ? "yes" : "no");

  spi_bus_config_t bus_config = {};
  bus_config.mosi_io_num = mosi_pin_;
  bus_config.miso_io_num = miso_pin_;
  bus_config.sclk_io_num = sck_pin_;
  bus_config.quadwp_io_num = -1;
  bus_config.quadhd_io_num = -1;
  bus_config.max_transfer_sz = 8;
  const esp_err_t init_err = spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_DISABLED);
  if (init_err != ESP_OK && init_err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "spi_bus_initialize after bitbang failed: %s", esp_err_to_name(init_err));
    return;
  }
  install_spi_device_(1000000, 0, true);
}

Nrf24Probe::TransferResult Nrf24Probe::run_bitbang_probe_() {
  TransferResult result;

  gpio_reset_pin(static_cast<gpio_num_t>(cs_pin_));
  gpio_reset_pin(static_cast<gpio_num_t>(sck_pin_));
  gpio_reset_pin(static_cast<gpio_num_t>(mosi_pin_));
  gpio_reset_pin(static_cast<gpio_num_t>(miso_pin_));
  gpio_set_direction(static_cast<gpio_num_t>(cs_pin_), GPIO_MODE_OUTPUT);
  gpio_set_direction(static_cast<gpio_num_t>(sck_pin_), GPIO_MODE_OUTPUT);
  gpio_set_direction(static_cast<gpio_num_t>(mosi_pin_), GPIO_MODE_OUTPUT);
  gpio_set_direction(static_cast<gpio_num_t>(miso_pin_), GPIO_MODE_INPUT);
  gpio_set_pull_mode(static_cast<gpio_num_t>(miso_pin_), GPIO_FLOATING);
  gpio_set_level(static_cast<gpio_num_t>(cs_pin_), 1);
  gpio_set_level(static_cast<gpio_num_t>(sck_pin_), 0);
  gpio_set_level(static_cast<gpio_num_t>(mosi_pin_), 0);
  delay(2);

  gpio_set_level(static_cast<gpio_num_t>(cs_pin_), 0);
  result.raw_nop = bitbang_transfer_byte_(NOP);
  gpio_set_level(static_cast<gpio_num_t>(cs_pin_), 1);
  delayMicroseconds(20);

  gpio_set_level(static_cast<gpio_num_t>(cs_pin_), 0);
  result.raw_config_status = bitbang_transfer_byte_(REG_CONFIG);
  result.raw_config_value = bitbang_transfer_byte_(NOP);
  gpio_set_level(static_cast<gpio_num_t>(cs_pin_), 1);
  result.status = result.raw_nop;
  result.config = result.raw_config_value;
  delayMicroseconds(20);

  gpio_set_level(static_cast<gpio_num_t>(cs_pin_), 0);
  bitbang_transfer_byte_(REG_SETUP_AW);
  result.setup_aw_before = bitbang_transfer_byte_(NOP);
  gpio_set_level(static_cast<gpio_num_t>(cs_pin_), 1);
  delayMicroseconds(20);

  gpio_set_level(static_cast<gpio_num_t>(cs_pin_), 0);
  bitbang_transfer_byte_(static_cast<uint8_t>(W_REGISTER | REG_SETUP_AW));
  bitbang_transfer_byte_(0x03);
  gpio_set_level(static_cast<gpio_num_t>(cs_pin_), 1);
  delayMicroseconds(20);

  gpio_set_level(static_cast<gpio_num_t>(cs_pin_), 0);
  bitbang_transfer_byte_(REG_SETUP_AW);
  result.setup_aw_after = bitbang_transfer_byte_(NOP);
  gpio_set_level(static_cast<gpio_num_t>(cs_pin_), 1);

  result.writable_register_ok = result.setup_aw_after == 0x03;
  result.plausible = is_plausible_nrf24_status(result.raw_nop) || is_plausible_nrf24_status(result.raw_config_status) ||
                     result.writable_register_ok;
  return result;
}

uint8_t Nrf24Probe::bitbang_transfer_byte_(uint8_t tx) {
  uint8_t rx = 0;
  for (int bit = 7; bit >= 0; bit--) {
    gpio_set_level(static_cast<gpio_num_t>(mosi_pin_), (tx >> bit) & 0x01);
    delayMicroseconds(5);
    gpio_set_level(static_cast<gpio_num_t>(sck_pin_), 1);
    delayMicroseconds(5);
    rx = static_cast<uint8_t>((rx << 1) | gpio_get_level(static_cast<gpio_num_t>(miso_pin_)));
    gpio_set_level(static_cast<gpio_num_t>(sck_pin_), 0);
    delayMicroseconds(5);
  }
  return rx;
}

void Nrf24Probe::probe_output_pin_(int pin, int &low_level, int &high_level) {
  gpio_reset_pin(static_cast<gpio_num_t>(pin));
  gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_INPUT_OUTPUT);
  gpio_set_level(static_cast<gpio_num_t>(pin), 0);
  delay(2);
  low_level = gpio_get_level(static_cast<gpio_num_t>(pin));
  gpio_set_level(static_cast<gpio_num_t>(pin), 1);
  delay(2);
  high_level = gpio_get_level(static_cast<gpio_num_t>(pin));
}

int Nrf24Probe::read_miso_with_pull_(gpio_pull_mode_t pull_mode) {
  gpio_set_pull_mode(static_cast<gpio_num_t>(miso_pin_), pull_mode);
  delay(2);
  return gpio_get_level(static_cast<gpio_num_t>(miso_pin_));
}

uint8_t Nrf24Probe::read_register_(uint8_t reg) {
  const uint8_t tx[2] = {static_cast<uint8_t>(R_REGISTER | (reg & 0x1F)), NOP};
  uint8_t rx[2] = {0xFF, 0xFF};
  if (!transfer_(tx, rx, sizeof(tx))) {
    return 0xFF;
  }
  return rx[1];
}

void Nrf24Probe::write_register_(uint8_t reg, uint8_t value) {
  const uint8_t tx[2] = {static_cast<uint8_t>(W_REGISTER | (reg & 0x1F)), value};
  uint8_t rx[2] = {0xFF, 0xFF};
  transfer_(tx, rx, sizeof(tx));
}

bool Nrf24Probe::transfer_(const uint8_t *tx, uint8_t *rx, size_t len, bool manual_cs) {
  spi_transaction_t transaction = {};
  transaction.length = len * 8;
  transaction.tx_buffer = tx;
  transaction.rx_buffer = rx;
  if (manual_cs) {
    gpio_set_level(static_cast<gpio_num_t>(cs_pin_), 0);
  }
  const esp_err_t err = spi_device_transmit(spi_, &transaction);
  if (manual_cs) {
    gpio_set_level(static_cast<gpio_num_t>(cs_pin_), 1);
  }
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "spi_device_transmit failed: %s", esp_err_to_name(err));
    return false;
  }
  return true;
}

}  // namespace nrf24_probe
}  // namespace esphome
