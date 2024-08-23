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

bool NiceBusT4::validate_message_() {                    // проверка получившегося сообщения
  uint32_t at = this->rx_message_.size() - 1;       // номер последнего полученного байта
  uint8_t *data = &this->rx_message_[0];               // указатель на первый байт сообщения
  uint8_t new_byte = data[at];                      // последний полученный байт
  // Byte 0: HEADER1 (всегда 0x00)
  if (at == 0x00)
    return new_byte == 0x00;
  // Byte 1: HEADER2 (всегда 0x55)
  if (at == 1)
    return new_byte == START_CODE;

  // Byte 2: packet_size - количество байт дальше + 1
  // Проверка не проводится

  if (at == 2)
    return true;
  uint8_t packet_size = data[2];
  uint8_t length = (packet_size + 3); // длина ожидаемого сообщения понятна


  // Byte 3: Серия (ряд) кому пакет
  // Проверка не проводится
  //  uint8_t command = data[3];
  if (at == 3)
    return true;

  // Byte 4: Адрес кому пакет
  // Byte 5: Серия (ряд) от кого пакет
  // Byte 6: Адрес от кого пакет
  // Byte 7: Тип сообшения CMD или INF
  // Byte 8: Количество байт дальше за вычетом двух байт CRC в конце.

  if (at <= 8)
    // Проверка не проводится
    return true;

  uint8_t crc1 = (data[3] ^ data[4] ^ data[5] ^ data[6] ^ data[7] ^ data[8]);

  // Byte 9: crc1 = XOR (Byte 3 : Byte 8) XOR шести предыдущих байт
  if (at == 9)
    if (data[9] != crc1) {
      ESP_LOGW(TAG, "Received invalid message checksum 1 %02X!=%02X", data[9], crc1);
      std::string pretty_cmd1 = format_hex_pretty(rx_message_);
      ESP_LOGD(TAG,  "Получен пакет: %S ", pretty_cmd1.c_str() );
      return false;
    }
  // Byte 10:
  // ...

  // ждем пока поступят все данные пакета
  if (at  < length)
    return true;

  // считаем crc2
  uint8_t crc2 = data[10];
  for (uint8_t i = 11; i < length - 1; i++) {
    crc2 = (crc2 ^ data[i]);
  }

  if (data[length - 1] != crc2 ) {
    ESP_LOGW(TAG, "Received invalid message checksum 2 %02X!=%02X", data[length - 1], crc2);
    std::string pretty_cmd1 = format_hex_pretty(rx_message_);
    ESP_LOGD(TAG,  "Получен пакет: %S ", pretty_cmd1.c_str() );
    return false;
  }

  // Byte Last: packet_size
  //  if (at  ==  length) {
  if (data[length] != packet_size ) {
    ESP_LOGW(TAG, "Received invalid message size %02X!=%02X", data[length], packet_size);
    std::string pretty_cmd1 = format_hex_pretty(rx_message_);
    ESP_LOGD(TAG,  "Получен пакет: %S ", pretty_cmd1.c_str() );
    return false;
  }

  // Если сюда дошли - правильное сообщение получено и лежит в буфере rx_message_

  // Удаляем 0x00 в начале сообщения
  rx_message_.erase(rx_message_.begin());

  // для вывода пакета в лог
  std::string pretty_cmd = format_hex_pretty(rx_message_);
  ESP_LOGI(TAG,  "Получен пакет: %S ", pretty_cmd.c_str() );

  // здесь что-то делаем с сообщением
  parse_status_packet(rx_message_);



  // возвращаем false чтобы обнулить rx buffer
  return false;

}


}  // namespace nice_bust4
}  // namespace esphome