#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace nice_bust4 {
//UARTDevice
class NiceBusT4 : public uart::IDFUARTComponent, public Component {
  public:
    void setup() override;
    void loop() override;
    void dump_config() override;
};


}  // namespace nice_bust4
}  // namespace esphome