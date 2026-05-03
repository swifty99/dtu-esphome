#include "hoymiles_dtu.h"

#include "esphome/core/log.h"

namespace esphome {
namespace hoymiles_dtu {

static const char* const TAG = "hoymiles_dtu";

void HoymilesDtuComponent::setup() {
  ESP_LOGCONFIG(TAG, "Hello from Hoymiles DTU");
}

void HoymilesDtuComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Hello from Hoymiles DTU");
}

}  // namespace hoymiles_dtu
}  // namespace esphome
