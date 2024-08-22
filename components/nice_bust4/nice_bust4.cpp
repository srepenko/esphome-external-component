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


    uint32_t baudrate;
    uint8_t master_tx_buf[2];
    master_tx_buf[0] = 0x55; // sync byte
    master_tx_buf[1] = 0x77;
    uint8_t dummy = 0;
    uart_flush_input(this->uart_num_);
    uart_get_baudrate(this->uart_num_, &baudrate);
#define LIN_BREAK_BAUDRATE(BAUD) ((BAUD * 9) / 13)
    uart_set_baudrate(this->uart_num_, LIN_BREAK_BAUDRATE(baudrate));
}

void NiceBusT4::loop() {

}

void NiceBusT4::dump_config(){
    ESP_LOGCONFIG(TAG, "NiceBusT4");
}

}  // namespace nice_bust4
}  // namespace esphome