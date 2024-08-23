#include "cover.h"
#include "esphome/core/log.h"


namespace esphome {
namespace nice_bust4 {

static const char *TAG = "NiceBusT4Cover";

using namespace esphome::cover;

CoverTraits NiceBusT4Cover::get_traits() {
  auto traits = CoverTraits();
  traits.set_supports_position(true);
  traits.set_supports_stop(true);
  return traits;
}

void NiceBusT4Cover::control(const CoverCall &call) {
  /*
  if (call.get_stop()) {
    // uint8_t data[2] = {CONTROL, STOP};
    this->tx_buffer_.push(gen_control_cmd(STOP));
    this->tx_buffer_.push(gen_inf_cmd(FOR_CU, INF_STATUS, GET));   //Состояние ворот (Открыто/Закрыто/Остановлено)
    if (is_walky) {
      tx_buffer_.push(gen_inf_cmd((uint8_t)(this->to_addr >> 8), (uint8_t)(this->to_addr & 0xFF), FOR_CU, CUR_POS, GET, 0x00, {0x01}, 1));  // запрос текущей позиции для энкодера
    }
    else {
      this->tx_buffer_.push(gen_inf_cmd(FOR_CU, CUR_POS, GET));    // запрос условного текущего положения привода
    }


  } else if (call.get_position().has_value()) {
    auto pos = *call.get_position();
    if (pos != this->position) {
      if (pos == COVER_OPEN) {
        this->tx_buffer_.push(gen_control_cmd(OPEN));

      } else if (pos == COVER_CLOSED) {
        this->tx_buffer_.push(gen_control_cmd(CLOSE));

      } //else {
        //  uint8_t data[3] = {CONTROL, SET_POSITION, (uint8_t)(pos * 100)};
        //  this->send_command_(data, 3);
        //}
    }
  }
  */
}

void NiceBusT4Cover::setup() {
}

void NiceBusT4Cover::loop() {
  uint8_t data[128];
  int length = 0;
  ESP_ERROR_CHECK(uart_get_buffered_data_len(this->parent_->uart_num_, (size_t*)&length));
  if (length > 0) {
    length = uart_read_bytes(this->parent_->uart_num_, data, length, 100);
    std::string pretty_cmd1 = format_hex_pretty(data, length);
    ESP_LOGI(TAG,  "Входящие данные: %S ", pretty_cmd1.c_str() );
    for (int i=0; i< length; ++i) {
      this->handle_char_(data[i]);
      this->last_uart_byte_ = now;
    }
    return;
  }
} //loop

void NiceBusT4Cover::dump_config() {
}

}  // namespace nice_bust4
}  // namespace esphome