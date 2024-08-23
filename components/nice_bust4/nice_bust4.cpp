#include "esphome/core/log.h"
#include "nice_bust4.h"
#include "esphome/core/helpers.h"  // для использования вспомогательных функция работ со строками

namespace esphome {
namespace nice_bust4 {

static const char *TAG = "nice_bust4.component";

void NiceBusT4::setup() {

}

void NiceBusT4::loop() {

    // разрешаем отправку каждые 100 ms
    const uint32_t now = millis();

    uint8_t data[128];
    int length = 0;
    ESP_ERROR_CHECK(uart_get_buffered_data_len(this->uart_num_, (size_t*)&length));
    if (length > 0) {
        length = uart_read_bytes(this->uart_num_, data, length, 100);
        std::string pretty_cmd1 = format_hex_pretty(data, length);
        ESP_LOGI(TAG,  "Входящие данные: %S ", pretty_cmd1.c_str() );
        for (int i=0; i< length; ++i) {
            this->handle_char_(data[i]);
            this->last_uart_byte_ = now;
        }
        return;
    }
}

void NiceBusT4::dump_config(){
    ESP_LOGCONFIG(TAG, "NiceBusT4");
}

void NiceBusT4::handle_char_(uint8_t c) {
  this->rx_message_.push_back(c);                      // кидаем байт в конец полученного сообщения
  if (!this->validate_message_()) {                    // проверяем получившееся сообщение
    this->rx_message_.clear();                         // если проверка не прошла, то в сообщении мусор, нужно удалить
  }
}



}  // namespace nice_bust4
}  // namespace esphome