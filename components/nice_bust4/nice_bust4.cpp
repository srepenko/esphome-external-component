#include "esphome/core/log.h"
#include "nice_bust4.h"
#include "esphome/core/helpers.h"  // для использования вспомогательных функция работ со строками
#include "esphome/components/uart/uart_component_esp_idf.h"

namespace esphome {
namespace nice_bust4 {

static const char *TAG = "nice_bust4.component";

void NiceBusT4::setup() {
}

void NiceBusT4::loop() {
    uint8_t data[128];
    int length = 0;
    this->uart_num_ = uart::IDFUARTComponent::get_hw_serial_number();
    //ESP_ERROR_CHECK(uart_get_buffered_data_len(this->uart_->uart_num_, (size_t*)&length));
/*
    if (length > 0) {
        length = uart_read_bytes(this->uart_num_, data, length, 100);
        //"%02X"
        //ESP_LOGCONFIG(TAG, "%d", length);
        std::string pretty_cmd1 = format_hex_pretty(data, length);
        ESP_LOGD(TAG,  "Получен пакет: %S ", pretty_cmd1.c_str() );
    } else {
    
        //while (this->IDFUARTComponent::available() > 0) {
        //    uint8_t c;
        //    this->IDFUARTComponent::peek_byte(&c);
        //    ESP_LOGCONFIG(TAG, "%02X", c);
        //} //while
        if(millis()-this->last_update >1000) {
            //this->load_settings(119200);
            //uint32_t invert = 0;
            //if (this->tx_pin_ != nullptr && this->tx_pin_->is_inverted())
            // invert |= UART_SIGNAL_TXD_INV;
            //if (this->rx_pin_ != nullptr && this->rx_pin_->is_inverted())
            // invert |= UART_SIGNAL_RXD_INV;
            //uart_set_line_inverse(this->uart_num_, invert);


            uint32_t baudrate;
            uint8_t master_tx_buf[] = {0x55, 0x0D, 0x00, 0xFF, 0x00, 0x66, 0x08, 0x06, 0x97, 0x00, 0x04, 0x99, 0x00, 0x00, 0x9D, 0x0D};
        //    uint8_t master_tx_buf[] = {0x55, 0x0c, 0x00, 0xff, 0x00, 0x66, 0x01, 0x05, 0x9D, 0x01, 0x82, 0x03, 0x00, 0x80, 0x0c};
        //    master_tx_buf[0] = 0x55; // sync byte
        //    master_tx_buf[1] = 0x77;
            uint8_t dummy = 0x00;
            uint8_t lin_uart_num = this->uart_num_;
            uart_flush_input(lin_uart_num);
            uart_get_baudrate(lin_uart_num, &baudrate);
            #define LIN_BREAK_BAUDRATE(BAUD) ((BAUD * 9) / 9)
            uart_set_baudrate(lin_uart_num, LIN_BREAK_BAUDRATE(baudrate));
            uart_write_bytes(lin_uart_num, (char *)&dummy, 1);              // send a zero byte.  This call must be blocking.
            uart_wait_tx_done(lin_uart_num, 2);                             // shouldn't be necessary??
            uart_wait_tx_done(lin_uart_num, 2);                             // add 2nd uart_wait_tx_done per https://esp32.com/viewtopic.php?p=98456#p98456
            uart_set_baudrate(lin_uart_num, baudrate);                      // set baudrate back to normal after break is sent
            uart_write_bytes(lin_uart_num, (char *)master_tx_buf, sizeof(master_tx_buf));
            this->last_update = millis();
            std::string pretty_cmd1 = format_hex_pretty(master_tx_buf, sizeof(master_tx_buf));
            ESP_LOGD(TAG,  "Отправлен пакет: %S ", pretty_cmd1.c_str() );
            
        }
    }
*/
}

void NiceBusT4::dump_config(){
    ESP_LOGCONFIG(TAG, "NiceBusT4");
}

}  // namespace nice_bust4
}  // namespace esphome