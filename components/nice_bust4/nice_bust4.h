#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/uart/uart_component_esp_idf.h"

namespace esphome {
namespace nice_bust4 {
class NiceBusT4 : public uart::UARTDevice, public uart::IDFUARTComponent{
#class NiceBusT4 : public uart::IDFUARTComponent{
  public:
    void setup() override;
    void loop() override;
    void dump_config() override;
};


}  // namespace nice_bust4
}  // namespace esphome