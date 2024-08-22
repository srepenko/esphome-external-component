#include "esphome/core/log.h"
#include "nice_bust4.h"

namespace esphome {
namespace nice_bust4 {

static const char *TAG = "nice_bust4.component";

void NiceBusT4::setup() {
    uint32_t baudrate;
    uint8_t master_tx_buf[2];
    master_tx_buf[0] = 0x55; // sync byte
    master_tx_buf[1] = 0x77;
    uint8_t dummy = 0;
    uint8_t lin_uart_num = this->uart_num_;
    uart_flush_input(lin_uart_num);
    uart_get_baudrate(lin_uart_num, &baudrate);
#define LIN_BREAK_BAUDRATE(BAUD) ((BAUD * 9) / 13)
    uart_set_baudrate(lin_uart_num, LIN_BREAK_BAUDRATE(baudrate));
    uart_write_bytes(lin_uart_num, (char *)&dummy, 1); // send a zero byte.  This call must be blocking.
    uart_wait_tx_done(lin_uart_num, 2);                // shouldn't be necessary??
    uart_wait_tx_done(lin_uart_num, 2);                // add 2nd uart_wait_tx_done per https://esp32.com/viewtopic.php?p=98456#p98456
    uart_set_baudrate(lin_uart_num, baudrate);         // set baudrate back to normal after break is sent
    uart_write_bytes(lin_uart_num, (char *)master_tx_buf, sizeof(master_tx_buf));
}

void NiceBusT4::loop() {

}

void NiceBusT4::dump_config(){
    ESP_LOGCONFIG(TAG, "NiceBusT4");
}

}  // namespace nice_bust4
}  // namespace esphome