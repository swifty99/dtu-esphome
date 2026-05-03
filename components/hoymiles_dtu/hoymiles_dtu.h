#pragma once

#include "esphome/core/component.h"

namespace esphome {
namespace hoymiles_dtu {

class HoymilesDtuComponent : public Component {
 public:
  void setup() override;
  void dump_config() override;
};

}  // namespace hoymiles_dtu
}  // namespace esphome
