// Minimal stub of ESPHome's HAL for the native protocol unit test. protocol.cpp only needs the
// declaration of get_mac_address_raw (used by hm_generate_dtu_serial); the test provides the
// definition. The real header is not available off-device.
#pragma once

#include <cstdint>

namespace esphome {

void get_mac_address_raw(uint8_t *mac);

}  // namespace esphome
