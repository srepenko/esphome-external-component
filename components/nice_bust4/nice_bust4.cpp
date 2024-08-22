#include "esphome/core/log.h"
#include "nice_bust4.h"

namespace esphome {
namespace nice_bust4 {

static const char *TAG = "nice_bust4.component";

void NiceBusT4::setup() {
    this->load_settings(119200);
    uint32_t invert = 0;
    if (this->tx_pin_ != nullptr && this->tx_pin_->is_inverted())
     invert |= UART_SIGNAL_TXD_INV;
    if (this->rx_pin_ != nullptr && this->rx_pin_->is_inverted())
     invert |= UART_SIGNAL_RXD_INV;
    uart_set_line_inverse(this->uart_num_, invert);
}

void NiceBusT4::loop() {

}

void NiceBusT4::dump_config(){
    ESP_LOGCONFIG(TAG, "NiceBusT4");
}

}  // namespace nice_bust4
}  // namespace esphome