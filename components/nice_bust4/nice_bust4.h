#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/uart/uart_component_esp_idf.h"

namespace esphome {
namespace nice_bust4 {
#define LIN_BREAK_BAUDRATE(BAUD) ((BAUD * 9) / 13)
//class NiceBusT4 : public Component, public uart::UARTDevice{
class NiceBusT4 : public uart::IDFUARTComponent{
  public:
    void setup() override;
    void loop() override;
    void dump_config() override;
  protected:
    uint32_t last_update;
};


}  // namespace nice_bust4
}  // namespace esphome