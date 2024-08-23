#include "esphome/core/log.h"
#include "nice_bust4.h"
#include "esphome/core/helpers.h"  // для использования вспомогательных функция работ со строками

namespace esphome {
namespace nice_bust4 {

static const char *TAG = "nice_bust4.component";

void NiceBusT4::setup() {

}

void NiceBusT4::loop() {
    if ((millis() - this->last_update_) > 5000) {    // каждые 10 секунд   
    // если привод не определился с первого раза, попробуем позже
        std::vector<uint8_t> unknown = {0x55, 0x55};
        if (this->init_ok == false) {
          this->tx_buffer_.push(gen_inf_cmd(0x00, 0xff, FOR_ALL, WHO, GET, 0x00));
          this->tx_buffer_.push(gen_inf_cmd(0x00, 0xff, FOR_ALL, PRD, GET, 0x00)); //запрос продукта
        }        
        else if (this->class_gate_ == 0x55) init_device((uint8_t)(this->to_addr >> 8), (uint8_t)(this->to_addr & 0xFF), 0x04);  
        else if (this->manufacturer_ == unknown)  {
         init_device((uint8_t)(this->to_addr >> 8), (uint8_t)(this->to_addr & 0xFF), 0x04);  
        }
        this->last_update_ = millis();
    }  // if  каждую минуту


    // разрешаем отправку каждые 100 ms
    const uint32_t now = millis();
    if (now - this->last_uart_byte_ > 100) {
        this->ready_to_tx_ = true;
        this->last_uart_byte_ = now;
    }
    
    uint8_t data[128];
    int length = 0;
    ESP_ERROR_CHECK(uart_get_buffered_data_len(this->uart_num_, (size_t*)&length));
    if (length > 0) {
        length = uart_read_bytes(this->uart_num_, data, length, 100);
        //std::string pretty_cmd1 = format_hex_pretty(data, length);
        //ESP_LOGI(TAG,  "Входящие данные: %S ", pretty_cmd1.c_str() );
        for (int i=0; i< length; ++i) {
            this->handle_char_(data[i]);
            this->last_uart_byte_ = now;
        }
        return;
    }
    
    if (this->ready_to_tx_) {   // если можно отправлять
      if (!this->tx_buffer_.empty()) {  // если есть что отправлять
        this->send_array_cmd(this->tx_buffer_.front()); // отправляем первую команду в очереди
        this->tx_buffer_.pop();
        this->ready_to_tx_ = false;
      }
    }
    
}

void NiceBusT4::dump_config(){
    ESP_LOGCONFIG(TAG, "NiceBusT4");
}

void NiceBusT4::send_array_cmd (std::vector<uint8_t> data) {          // отправляет break + подготовленную ранее в массиве команду
  return send_array_cmd((const uint8_t *)data.data(), data.size());
}
void NiceBusT4::send_array_cmd (const uint8_t *data, size_t len) {
  // отправка данных в uart
  uint32_t baudrate;
  uint8_t dummy = 0x00;
  uart_flush_input(this->uart_num_);
  uart_get_baudrate(this->uart_num_, &baudrate);
  #define LIN_BREAK_BAUDRATE(BAUD) ((BAUD * 9) / 13)
  uart_set_baudrate(this->uart_num_, LIN_BREAK_BAUDRATE(baudrate));
  uart_write_bytes(this->uart_num_, (char *)&dummy, 1);              // send a zero byte.  This call must be blocking.
  uart_wait_tx_done(this->uart_num_, 2);                             // shouldn't be necessary??
  uart_wait_tx_done(this->uart_num_, 2);                             // add 2nd uart_wait_tx_done per https://esp32.com/viewtopic.php?p=98456#p98456
  uart_set_baudrate(this->uart_num_, baudrate);                      // set baudrate back to normal after break is sent
  uart_write_bytes(this->uart_num_, data, len);
  std::string pretty_cmd = format_hex_pretty((uint8_t*)&data[0], len);                    // для вывода команды в лог
  ESP_LOGI(TAG,  "Отправлено:    %S ", pretty_cmd.c_str() );
  
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
  //parse_status_packet(rx_message_);



  // возвращаем false чтобы обнулить rx buffer
  return false;

}

/*
void NiceBusT4::parse_status_packet (const std::vector<uint8_t> &data) {
  if ((data[1] == 0x0d) && (data[13] == 0xFD)) { // ошибка
    ESP_LOGE(TAG,  "Команда недоступна для этого устройства" );
  }

  if (((data[11] == 0x18) || (data[11] == 0x19)) && (data[13] == NOERR)) { // if evt
    ESP_LOGD(TAG, "Получен пакет EVT с данными. Последняя ячейка %d ", data[12]);
    std::vector<uint8_t> vec_data(this->rx_message_.begin() + 14, this->rx_message_.end() - 2);
    std::string str(this->rx_message_.begin() + 14, this->rx_message_.end() - 2);
    ESP_LOGI(TAG,  "Строка с данными: %S ", str.c_str() );
    std::string pretty_data = format_hex_pretty(vec_data);
    ESP_LOGI(TAG,  "Данные HEX %S ", pretty_data.c_str() );
    // получили пакет с данными EVT, начинаем разбирать

    if ((data[6] == INF) && (data[9] == FOR_CU)  && (data[11] == GET - 0x80) && (data[13] == NOERR)) { // интересуют ответы на запросы GET, пришедшие без ошибок от привода
      ESP_LOGI(TAG,  "Получен ответ на запрос %X ", data[10] );
      switch (data[10]) { // cmd_submnu
        case TYPE_M:
          //           ESP_LOGI(TAG,  "Тип привода %X",  data[14]);
          switch (data[14]) { //14
            case SLIDING:
              this->class_gate_ = SLIDING;
              //        ESP_LOGD(TAG, "Тип ворот: Откатные %#X ", data[14]);
              break;
            case SECTIONAL:
              this->class_gate_ = SECTIONAL;
              //        ESP_LOGD(TAG, "Тип ворот: Секционные %#X ", data[14]);
              break;
            case SWING:
              this->class_gate_ = SWING;
              //        ESP_LOGD(TAG, "Тип ворот: Распашные %#X ", data[14]);
              break;
            case BARRIER:
              this->class_gate_ = BARRIER;
              //        ESP_LOGD(TAG, "Тип ворот: Шлагбаум %#X ", data[14]);
              break;
            case UPANDOVER:
              this->class_gate_ = UPANDOVER;
              //        ESP_LOGD(TAG, "Тип ворот: Подъемно-поворотные %#X ", data[14]);
              break;
          }  // switch 14
          break; //  TYPE_M
        case INF_IO: // ответ на запрос положения концевика откатных ворот
          switch (data[16]) { //16
            case 0x00:
              ESP_LOGI(TAG, "  Концевик не сработал ");
              break; // 0x00
            case 0x01:
              ESP_LOGI(TAG, "  Концевик на закрытие ");
              this->position = COVER_CLOSED;
              break; //  0x01
            case 0x02:
              ESP_LOGI(TAG, "  Концевик на открытие ");
              this->position = COVER_OPEN;
              break; // 0x02

          }  // switch 16
          this->publish_state();  // публикуем состояние

          break; //  INF_IO


        //положение максимального открытия энкодера, открытия, закрытия

        case MAX_OPN:
          if (is_walky) {
            this->_max_opn = data[15];
            this->_pos_opn = data[15];
          }
          else {  
            this->_max_opn = (data[14] << 8) + data[15];
          }
          ESP_LOGI(TAG, "Максимальное положение энкодера: %d", this->_max_opn);
          break;

        case POS_MIN:
          this->_pos_cls = (data[14] << 8) + data[15];
          ESP_LOGI(TAG, "Положение закрытых ворот: %d", this->_pos_cls);
          break;

        case POS_MAX:
          if (((data[14] << 8) + data[15])>0x00) { // если в ответе от привода есть данные о положении открытия
          this->_pos_opn = (data[14] << 8) + data[15];}
          ESP_LOGI(TAG, "Положение открытых ворот: %d", this->_pos_opn);
          break;

        case CUR_POS:
          if (is_walky) {
            this->_pos_usl = data[15];
          }
          else {
            this->_pos_usl = (data[14] << 8) + data[15];
          }
          this->position = (_pos_usl - _pos_cls) * 1.0f / (_pos_opn - _pos_cls);
          ESP_LOGI(TAG, "Условное положение ворот: %d, положение в %%: %f", _pos_usl, (_pos_usl - _pos_cls) * 100.0f / (_pos_opn - _pos_cls));
          this->publish_state();  // публикуем состояние
          break;

        case 0x01:
          switch (data[14]) {
            case OPENED:
              ESP_LOGI(TAG, "  Ворота открыты");
              this->position = COVER_OPEN;
              this->current_operation = COVER_OPERATION_IDLE;
              break;
            case CLOSED:
              ESP_LOGI(TAG, "  Ворота закрыты");
              this->position = COVER_CLOSED;
              this->current_operation = COVER_OPERATION_IDLE;
              break;
            case 0x01:
              ESP_LOGI(TAG, "  Ворота остановлены");
              this->current_operation = COVER_OPERATION_IDLE;
              //          this->position = COVER_OPEN;
              break;
            case 0x00:
              ESP_LOGI(TAG, "  Статус ворот неизвестен");
              this->current_operation = COVER_OPERATION_IDLE;
              break;
             case 0x0b:
              ESP_LOGI(TAG, "  Поиск положений сделан");
              this->current_operation = COVER_OPERATION_IDLE;
              break;
              case STA_OPENING:
              ESP_LOGI(TAG, "  Идёт открывание");
              this->current_operation = COVER_OPERATION_OPENING;
              break;
              case STA_CLOSING:
              ESP_LOGI(TAG, "  Идёт закрывание");
              this->current_operation = COVER_OPERATION_CLOSING;
              break;
          }  // switch
          this->publish_state();  // публикуем состояние
          break;

          //      default: // cmd_mnu
        case AUTOCLS:
          this->autocls_flag = data[14];
          break;
          
        case PH_CLS_ON:
          this->photocls_flag = data[14];
          break;  
          
        case ALW_CLS_ON:
          this->alwayscls_flag = data[14];
          break;  
          
      } // switch cmd_submnu
    } // if ответы на запросы GET, пришедшие без ошибок от привода
    
    if ((data[6] == INF) && (data[9] == FOR_CU)  && (data[11] == SET - 0x80) && (data[13] == NOERR)) { // интересуют ответы на запросы SET, пришедшие без ошибок от привода    
      switch (data[10]) { // cmd_submnu
        case AUTOCLS:
          tx_buffer_.push(gen_inf_cmd(FOR_CU, AUTOCLS, GET)); // Автозакрытие
          break;
          
        case PH_CLS_ON:
          tx_buffer_.push(gen_inf_cmd(FOR_CU, PH_CLS_ON, GET)); // Закрыть после Фото
          break;  
          
        case ALW_CLS_ON:
          tx_buffer_.push(gen_inf_cmd(FOR_CU, ALW_CLS_ON, GET)); // Всегда закрывать
          break;  
      }// switch cmd_submnu
    }// if ответы на запросы SET, пришедшие без ошибок от привода

    if ((data[6] == INF) && (data[9] == FOR_ALL)  && ((data[11] == GET - 0x80) || (data[11] == GET - 0x81)) && (data[13] == NOERR)) { // интересуют FOR_ALL ответы на запросы GET, пришедшие без ошибок

      switch (data[10]) {
        case MAN:
          //       ESP_LOGCONFIG(TAG, "  Производитель: %S ", str.c_str());
          this->manufacturer_.assign(this->rx_message_.begin() + 14, this->rx_message_.end() - 2);
          break;
        case PRD:
          if (((uint8_t)(this->oxi_addr >> 8) == data[4]) && ((uint8_t)(this->oxi_addr & 0xFF) == data[5])) { // если пакет от приемника
//            ESP_LOGCONFIG(TAG, "  Приёмник: %S ", str.c_str());
            this->oxi_product.assign(this->rx_message_.begin() + 14, this->rx_message_.end() - 2);
          } // если пакет от приемника
          else if (((uint8_t)(this->to_addr >> 8) == data[4]) && ((uint8_t)(this->to_addr & 0xFF) == data[5])) { // если пакет от контроллера привода
//            ESP_LOGCONFIG(TAG, "  Привод: %S ", str.c_str());
            this->product_.assign(this->rx_message_.begin() + 14, this->rx_message_.end() - 2);
            std::vector<uint8_t> wla1 = {0x57,0x4C,0x41,0x31,0x00,0x06,0x57}; // для понимания, что привод Walky
            if (this->product_ == wla1) { 
              this->is_walky = true;
         //     ESP_LOGCONFIG(TAG, "  Привод WALKY!: %S ", str.c_str());
                                        }
          }
          break;
        case HWR:
          if (((uint8_t)(this->oxi_addr >> 8) == data[4]) && ((uint8_t)(this->oxi_addr & 0xFF) == data[5])) { // если пакет от приемника
            this->oxi_hardware.assign(this->rx_message_.begin() + 14, this->rx_message_.end() - 2);
          }
          else if (((uint8_t)(this->to_addr >> 8) == data[4]) && ((uint8_t)(this->to_addr & 0xFF) == data[5])) { // если пакет от контроллера привода          
          this->hardware_.assign(this->rx_message_.begin() + 14, this->rx_message_.end() - 2);
          } //else
          break;
        case FRM:
          if (((uint8_t)(this->oxi_addr >> 8) == data[4]) && ((uint8_t)(this->oxi_addr & 0xFF) == data[5])) { // если пакет от приемника
            this->oxi_firmware.assign(this->rx_message_.begin() + 14, this->rx_message_.end() - 2);
          }
          else if (((uint8_t)(this->to_addr >> 8) == data[4]) && ((uint8_t)(this->to_addr & 0xFF) == data[5])) { // если пакет от контроллера привода          
            this->firmware_.assign(this->rx_message_.begin() + 14, this->rx_message_.end() - 2);
          } //else
          break;
        case DSC:
          if (((uint8_t)(this->oxi_addr >> 8) == data[4]) && ((uint8_t)(this->oxi_addr & 0xFF) == data[5])) { // если пакет от приемника
            this->oxi_description.assign(this->rx_message_.begin() + 14, this->rx_message_.end() - 2);
          }
          else if (((uint8_t)(this->to_addr >> 8) == data[4]) && ((uint8_t)(this->to_addr & 0xFF) == data[5])) { // если пакет от контроллера привода          
            this->description_.assign(this->rx_message_.begin() + 14, this->rx_message_.end() - 2);
          } //else
          break;
        case WHO:
          if (data[12] == 0x01) {
            if (data[14] == 0x04) { // привод
              this-> to_addr = ((uint16_t)data[4] << 8) | data[5];
              this->init_ok = true;
     //         init_device(data[4], data[5], data[14]);
            }
            else if (data[14] == 0x0A) { // приёмник
              this-> oxi_addr = ((uint16_t)data[4] << 8) | data[5];
              init_device(data[4], data[5], data[14]);
            }
          }
          break;
      }  // switch

    }  // if  FOR_ALL ответы на запросы GET, пришедшие без ошибок

    if ((data[9] == 0x0A) &&  (data[10] == 0x25) &&  (data[11] == 0x01) &&  (data[12] == 0x0A) &&  (data[13] == NOERR)) { //  пакеты от приемника с информацией о списке пультов, пришедшие без ошибок
      ESP_LOGCONFIG(TAG, "Номер пульта: %X%X%X%X, команда: %X, кнопка: %X, режим: %X, счётчик нажатий: %d", vec_data[5], vec_data[4], vec_data[3], vec_data[2], vec_data[8] / 0x10, vec_data[5] / 0x10, vec_data[7] + 0x01, vec_data[6]);
    }  // if

    if ((data[9] == 0x0A) &&  (data[10] == 0x26) &&  (data[11] == 0x41) &&  (data[12] == 0x08) &&  (data[13] == NOERR)) { //  пакеты от приемника с информацией о считанной кнопке пульта
      ESP_LOGCONFIG(TAG, "Кнопка %X, номер пульта: %X%X%X%X", vec_data[0] / 0x10, vec_data[0] % 0x10, vec_data[1], vec_data[2], vec_data[3]);
    }  // if

  } //  if evt



  //else if ((data[14] == NOERR) && (data[1] > 0x0d)) {  // иначе пакет Responce - подтверждение полученной команды
  else if (data[1] > 0x0d) {  // иначе пакет Responce - подтверждение полученной команды
    ESP_LOGD(TAG, "Получен пакет RSP");
    std::vector<uint8_t> vec_data(this->rx_message_.begin() + 12, this->rx_message_.end() - 3);
    std::string str(this->rx_message_.begin() + 12, this->rx_message_.end() - 3);
    ESP_LOGI(TAG,  "Строка с данными: %S ", str.c_str() );
    std::string pretty_data = format_hex_pretty(vec_data);
    ESP_LOGI(TAG,  "Данные HEX %S ", pretty_data.c_str() );
    switch (data[9]) { // cmd_mnu
      case FOR_CU:
        ESP_LOGI(TAG,  "Пакет контроллера привода" );
        switch (data[10] + 0x80) { // sub_inf_cmd
          case RUN:
            ESP_LOGI(TAG,  "Подменю RUN" );
            switch (data[11] - 0x80) { // sub_run_cmd1
              case SBS:
                ESP_LOGI(TAG,  "Команда: Пошагово" );
                break; // SBS
              case STOP:
                ESP_LOGI(TAG,  "Команда: STOP" );
                break; // STOP
              case OPEN:
                ESP_LOGI(TAG,  "Команда: OPEN" );
                this->current_operation = COVER_OPERATION_OPENING;
                break; // OPEN
              case CLOSE:
                ESP_LOGI(TAG,  "Команда: CLOSE" );
                this->current_operation = COVER_OPERATION_CLOSING;                
                break;  // CLOSE
              case P_OPN1:
                ESP_LOGI(TAG,  "Команда: Частичное открывание" );
                break; // P_OPN1
              case STOPPED:
                this->current_operation = COVER_OPERATION_IDLE;
                ESP_LOGI(TAG, "Команда: Остановлено");
                break; // STOPPED
              case ENDTIME:
                ESP_LOGI(TAG, "Операция завершена по таймауту");
                break; // 

            } // switch sub_run_cmd1
            
            switch (data[11]) { // sub_run_cmd2
              case STA_OPENING:
                ESP_LOGI(TAG,  "Операция: Открывается" );
                this->current_operation = COVER_OPERATION_OPENING;
                break; // OPEN
              case STA_CLOSING:
                ESP_LOGI(TAG,  "Операция: Закрывается" );
                this->current_operation = COVER_OPERATION_CLOSING;                
                break;  // CLOSING
              case CLOSED:
                ESP_LOGI(TAG,  "Операция: Закрыто" );
                this->position = COVER_CLOSED;
                this->current_operation = COVER_OPERATION_IDLE;
                break;  // CLOSED  
              case OPENED:
                this->position = COVER_OPEN;
                ESP_LOGI(TAG, "Операция: Открыто");
                this->current_operation = COVER_OPERATION_IDLE;
                break;
              case STOPPED:
                this->current_operation = COVER_OPERATION_IDLE;
                ESP_LOGI(TAG, "Операция: Остановлено");
                break;
              default: // sub_run_cmd1
                ESP_LOGI(TAG,  "Операция: %X", data[11] );                            
            } // switch sub_run_cmd2                 
            this->publish_state();  // публикуем состояние
            break; //RUN

          case STA:
            ESP_LOGI(TAG,  "Подменю Статус в движении" );
            switch (data[11]) { // sub_run_cmd2
              case STA_OPENING:
                ESP_LOGI(TAG,  "Движение: Открывается" );
                this->current_operation = COVER_OPERATION_OPENING;
                break; // STA_OPENING
              case STA_CLOSING:
                ESP_LOGI(TAG,  "Движение: Закрывается" );
                this->current_operation = COVER_OPERATION_CLOSING;
                break; // STA_CLOSING
              case CLOSED:
                ESP_LOGI(TAG,  "Движение: Закрыто" );
                this->position = COVER_CLOSED;
                this->current_operation = COVER_OPERATION_IDLE;
                break;  // CLOSED  
              case OPENED:
                this->position = COVER_OPEN;
                ESP_LOGI(TAG, "Движение: Открыто");
                this->current_operation = COVER_OPERATION_IDLE;
                break;
              case STOPPED:
                this->current_operation = COVER_OPERATION_IDLE;
                ESP_LOGI(TAG, "Движение: Остановлено");
                break;
              default: // sub_run_cmd2
                ESP_LOGI(TAG,  "Движение: %X", data[11] );

                
            } // switch sub_run_cmd2

            this->_pos_usl = (data[12] << 8) + data[13];
            this->position = (_pos_usl - _pos_cls) * 1.0f / (_pos_opn - _pos_cls);
            ESP_LOGD(TAG, "Условное положение ворот: %d, положение в %%: %f", _pos_usl, (_pos_usl - _pos_cls) * 100.0f / (_pos_opn - _pos_cls));
            this->publish_state();  // публикуем состояние

            break; //STA





          default: // sub_inf_cmd
            ESP_LOGI(TAG,  "Подменю %X", data[10] );
        }  // switch sub_inf_cmd

        break; // Пакет контроллера привода
      case CONTROL:
        ESP_LOGI(TAG,  "Пакет CONTROL" );
        break; // CONTROL
      case FOR_ALL:
        ESP_LOGI(TAG,  "Пакет для всех" );
        break; // FOR_ALL
      case 0x0A:
        ESP_LOGI(TAG,  "Пакет приёмника" );
        break; // пакет приёмника
      default: // cmd_mnu
        ESP_LOGI(TAG,  "Меню %X", data[9] );
    }  // switch  cmd_mnu


  } // else


  ///////////////////////////////////////////////////////////////////////////////////


  // RSP ответ (ReSPonce) на простой прием команды CMD, а не ее выполнение. Также докладывает о завершении операции.
  /+ if ((data[1] == 0x0E) && (data[6] == CMD) && (data[9] == FOR_CU) && (data[10] == CUR_MAN) && (data[12] == 0x19)) { // узнаём пакет статуса по содержимому в определённых байтах
     //  ESP_LOGD(TAG, "Получен пакет RSP. cmd = %#x", data[11]);

     switch (data[11]) {
       case OPENING:
         this->current_operation = COVER_OPERATION_OPENING;
         ESP_LOGD(TAG, "Статус: Открывается");
         break;
       case CLOSING:
         this->current_operation = COVER_OPERATION_CLOSING;
         ESP_LOGD(TAG, "Статус: Закрывается");
         break;
       case OPENED:
         this->position = COVER_OPEN;
         ESP_LOGD(TAG, "Статус: Открыто");
         this->current_operation = COVER_OPERATION_IDLE;
         break;


       case CLOSED:
         this->position = COVER_CLOSED;
         ESP_LOGD(TAG, "Статус: Закрыто");
         this->current_operation = COVER_OPERATION_IDLE;
         break;
       case STOPPED:
         this->current_operation = COVER_OPERATION_IDLE;
         ESP_LOGD(TAG, "Статус: Остановлено");
         break;

     }  // switch

     this->publish_state();  // публикуем состояние

    } //if
  */
  /*
    // статус после достижения концевиков
    if ((data[1] == 0x0E) && (data[6] == CMD) && (data[9] == FOR_CU) && (data[10] == CUR_MAN) &&  (data[12] == 0x00)) { // узнаём пакет статуса по содержимому в определённых байтах
      ESP_LOGD(TAG, "Получен пакет концевиков. Статус = %#x", data[11]);
      switch (data[11]) {
        case OPENED:
          this->position = COVER_OPEN;
          ESP_LOGD(TAG, "Статус: Открыто");
          this->current_operation = COVER_OPERATION_IDLE;
          break;
        case CLOSED:
          this->position = COVER_CLOSED;
          ESP_LOGD(TAG, "Статус: Закрыто");
          this->current_operation = COVER_OPERATION_IDLE;
          break;
        case OPENING:
          this->current_operation = COVER_OPERATION_OPENING;
          ESP_LOGD(TAG, "Статус: Открывается");
          break;
        case CLOSING:
          this->current_operation = COVER_OPERATION_CLOSING;
          ESP_LOGD(TAG, "Статус: Закрывается");
          break;
      } //switch
      this->publish_state();  // публикуем состояние
    } //if
  */
  // STA = 0x40,   // статус в движении
  /*
    if ((data[1] == 0x0E) && (data[6] == CMD) && (data[9] == FOR_CU) && (data[10] == STA) ) { // узнаём пакет статуса по содержимому в определённых байтах
      uint16_t ipos = (data[12] << 8) + data[13];
      ESP_LOGD(TAG, "Текущий маневр: %#X Позиция: %#X %#X, ipos = %#x,", data[11], data[12], data[13], ipos);
      this->position = ipos / 2100.0f; // передаем позицию компоненту

      switch (data[11]) {
        case OPENING:
          this->current_operation = COVER_OPERATION_OPENING;
          ESP_LOGD(TAG, "Статус: Открывается");
          break;

        case OPENING2:
          this->current_operation = COVER_OPERATION_OPENING;
          ESP_LOGD(TAG, "Статус: Открывается");
          break;

        case CLOSING:
          this->current_operation = COVER_OPERATION_CLOSING;
          ESP_LOGD(TAG, "Статус: Закрывается");
          break;
        case CLOSING2:
          this->current_operation = COVER_OPERATION_CLOSING;
          ESP_LOGD(TAG, "Статус: Закрывается");
          break;
        case OPENED:
          this->position = COVER_OPEN;
          this->current_operation = COVER_OPERATION_IDLE;
          ESP_LOGD(TAG, "Статус: Открыто");
          //      this->current_operation = COVER_OPERATION_OPENING;
          //    ESP_LOGD(TAG, "Статус: Открывается");
          break;
        case CLOSED:
          this->position = COVER_CLOSED;
          this->current_operation = COVER_OPERATION_IDLE;
          ESP_LOGD(TAG, "Статус: Закрыто");
          //      this->current_operation = COVER_OPERATION_CLOSING;
          //ESP_LOGD(TAG, "Статус: Закрывается");
          break;
        case STOPPED:
          this->current_operation = COVER_OPERATION_IDLE;
          ESP_LOGD(TAG, "Статус: Остановлено");
          break;

      }  // switch

      this->publish_state();  // публикуем состояние

    } //if
  +/


  ////////////////////////////////////////////////////////////////////////////////////////
} // function
*/

// инициализация устройства
void NiceBusT4::init_device (const uint8_t addr1, const uint8_t addr2, const uint8_t device ) {
  if (device == FOR_CU) {
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, device, TYPE_M, GET, 0x00)); // запрос типа привода
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, FOR_ALL, MAN, GET, 0x00)); // запрос производителя
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, FOR_ALL, FRM, GET, 0x00)); //  запрос прошивки
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, FOR_ALL, PRD, GET, 0x00)); //запрос продукта
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, FOR_ALL, HWR, GET, 0x00)); //запрос железа
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, device, POS_MAX, GET, 0x00));   //запрос позиции открытия
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, device, POS_MIN, GET, 0x00)); // запрос позиции закрытия
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, FOR_ALL, DSC, GET, 0x00)); //запрос описания
    if (is_walky) {
      tx_buffer_.push(gen_inf_cmd(addr1, addr2, device, MAX_OPN, GET, 0x00, {0x01}, 1));   // запрос максимального значения для энкодера
      tx_buffer_.push(gen_inf_cmd(addr1, addr2, device, CUR_POS, GET, 0x00, {0x01}, 1));  // запрос текущей позиции для энкодера
    }
    else { 
      tx_buffer_.push(gen_inf_cmd(addr1, addr2, device, MAX_OPN, GET, 0x00));   // запрос максимального значения для энкодера
      tx_buffer_.push(gen_inf_cmd(addr1, addr2, device, CUR_POS, GET, 0x00));  // запрос текущей позиции для энкодера
    }  
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, device, INF_STATUS, GET, 0x00)); //Состояние ворот (Открыто/Закрыто/Остановлено)
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, device, AUTOCLS, GET, 0x00)); // Автозакрытие
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, device, PH_CLS_ON, GET, 0x00)); // Закрыть после Фото
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, device, ALW_CLS_ON, GET, 0x00)); // Всегда закрывать
  }
  if (device == FOR_OXI) {
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, FOR_ALL, PRD, GET, 0x00)); //запрос продукта
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, FOR_ALL, HWR, GET, 0x00)); //запрос железа    
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, FOR_ALL, FRM, GET, 0x00)); //  запрос прошивки    
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, FOR_ALL, DSC, GET, 0x00)); //запрос описания    
  }
  
}

//формирование команды управления
std::vector<uint8_t> NiceBusT4::gen_control_cmd(const uint8_t control_cmd) {
  std::vector<uint8_t> frame = {(uint8_t)(this->to_addr >> 8), (uint8_t)(this->to_addr & 0xFF), (uint8_t)(this->from_addr >> 8), (uint8_t)(this->from_addr & 0xFF)}; // заголовок
  frame.push_back(CMD);  // 0x01
  frame.push_back(0x05);
  uint8_t crc1 = (frame[0] ^ frame[1] ^ frame[2] ^ frame[3] ^ frame[4] ^ frame[5]);
  frame.push_back(crc1);
  frame.push_back(CONTROL);
  frame.push_back(RUN);
  frame.push_back(control_cmd);
  frame.push_back(0x64); // OFFSET CMD, DPRO924 отказался работать с 0x00, хотя остальные приводы реагировали на команды
  uint8_t crc2 = (frame[7] ^ frame[8] ^ frame[9] ^ frame[10]);
  frame.push_back(crc2);
  uint8_t f_size = frame.size();
  frame.push_back(f_size);
  frame.insert(frame.begin(), f_size);
  frame.insert(frame.begin(), START_CODE);

  // для вывода команды в лог
  //  std::string pretty_cmd = format_hex_pretty(frame);
  //  ESP_LOGI(TAG,  "Сформирована команда: %S ", pretty_cmd.c_str() );

  return frame;
}

// формирование команды INF с данными и без
std::vector<uint8_t> NiceBusT4::gen_inf_cmd(const uint8_t to_addr1, const uint8_t to_addr2, const uint8_t whose, const uint8_t inf_cmd, const uint8_t run_cmd, const uint8_t next_data, const std::vector<uint8_t> &data, size_t len) {
  std::vector<uint8_t> frame = {to_addr1, to_addr2, (uint8_t)(this->from_addr >> 8), (uint8_t)(this->from_addr & 0xFF)}; // заголовок
  frame.push_back(INF);  // 0x08 mes_type
  frame.push_back(0x06 + len); // mes_size
  uint8_t crc1 = (frame[0] ^ frame[1] ^ frame[2] ^ frame[3] ^ frame[4] ^ frame[5]);
  frame.push_back(crc1);
  frame.push_back(whose);
  frame.push_back(inf_cmd);
  frame.push_back(run_cmd);
  frame.push_back(next_data); // next_data
  frame.push_back(len);
  if (len > 0) {
    frame.insert(frame.end(), data.begin(), data.end()); // блок данных
  }
  uint8_t crc2 = frame[7];
  for (size_t i = 8; i < 12 + len; i++) {
    crc2 = crc2 ^ frame[i];
  }
  frame.push_back(crc2);
  uint8_t f_size = frame.size();
  frame.push_back(f_size);
  frame.insert(frame.begin(), f_size);
  frame.insert(frame.begin(), START_CODE);

  // для вывода команды в лог
  //  std::string pretty_cmd = format_hex_pretty(frame);
  //  ESP_LOGI(TAG,  "Сформирован INF пакет: %S ", pretty_cmd.c_str() );

  return frame;

}

}  // namespace nice_bust4
}  // namespace esphome