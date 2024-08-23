#pragma once

#include "esphome.h"
#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/uart/uart_component_esp_idf.h"

namespace esphome {
namespace nice_bust4 {
//#define LIN_BREAK_BAUDRATE(BAUD) ((BAUD * 9) / 13)


//class NiceBusT4 : public Component, public uart::UARTDevice{
class NiceBusT4 : public uart::IDFUARTComponent{
  public:
    void setup() override;
    void loop() override;
    void dump_config() override;
  protected:
//    void send_command_(const uint8_t *data, uint8_t len);


    std::vector<uint8_t> rx_message_;                          // здесь побайтно накапливается принятое сообщение
//    std::queue<std::vector<uint8_t>> tx_buffer_;             // очередь команд для отправки	
//    bool ready_to_tx_{true};	                           // флаг возможности отправлять команды
	
    uint32_t update_interval_{500};
    uint32_t last_update_{0};
    uint32_t last_uart_byte_{0};

    uint8_t last_published_op_;
    float last_published_pos_;

    void handle_char_(uint8_t c);                                         // обработчик полученного байта
    bool validate_message_();                                         // функция проверки полученного сообщения

    uint32_t last_update;
};


}  // namespace nice_bust4
}  // namespace esphome