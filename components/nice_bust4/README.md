```yaml
substitutions:
  name: nice-bust4
  #name: nice-wifi
  device_description: "Nice BusT4"
  external_components_source: github://srepenko/esphome-external-component

esphome:
  name: ${name}
  platformio_options:
    board_build.f_flash: 40000000L
    board_build.flash_mode: dio
    board_build.flash_size: 4MB

esp32:
  variant: ESP32C3
  board: esp32-c3-devkitm-1
  framework:
    #type: arduino
    type: esp-idf

external_components:
  - source: ${external_components_source}
    refresh: 0s

# Enable logging
logger:
  level: DEBUG
  #tx_buffer_size: 2048
  #baud_rate: 0

ota:
  - platform: esphome
    password: !secret esphome_ota_key

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

  # Enable fallback hotspot (captive portal) in case wifi connection fails
  ap:
    ssid: "Nice-Bust4 Fallback Hotspot"
    password: "oKYgkqxxAhIS"

captive_portal:

web_server:
  version: 3
#  js_include: "www.js"
#  js_url: ""
#  version: 2  

# Set statul led
status_led:
  pin: 
    number: GPIO8
    inverted: true

uart:
  id: uart_0
  baud_rate: 19200
  tx_pin: GPIO21
  rx_pin: GPIO20

nice_bust4:
  id: nicebust4

# Enable Home Assistant API
api:
  encryption:
    key: !secret esphome_encryption_key
  services:
# для отправки hex команд на привод
  - service: raw_command
    variables:
        raw_cmd: string
    then:
      lambda: |-
         nicebust4 -> NiceBusT4::send_raw_cmd(raw_cmd);
         
  - service: send_inf_command
    variables:
       to_addr: string
       whose: string
       command: string
       type_command: string
       next_data: string
       data_on: bool
       data_command: string
    then:
      lambda: |-
        nicebust4 -> NiceBusT4::send_inf_cmd(to_addr, whose, command, type_command, next_data, data_on, data_command);

# распознавание длины створки
  - service: gate_length_recognition
    then:
      lambda: |-
         nicebust4 -> NiceBusT4::set_mcu("0b","01");

# распознавание устройств BlueBus
  - service: devices_recognition
    then:
      lambda: |-
         nicebust4 -> NiceBusT4::set_mcu("0a","01");         

# усилие при закрытии
  - service: closing_force
    variables:
      force: string
    then:
      lambda: |-
         nicebust4 -> NiceBusT4::set_mcu("4b", force);         

# усилие при открытии
  - service: opening_force
    variables:
      force: string
    then:
      lambda: |-
         nicebust4 -> NiceBusT4::set_mcu("4a", force);

cover:
- platform: nice_bust4
  name: "Nice Cover"
  device_class: gate
  id: nice_cover
  nice_bust4_id: nicebust4
##  address: 0x0003            # адрес привода
##  use_address: 0x0081        # адрес шлюза

# отключаем автозакрытие ворот, если это нужно для погрузочно-разгрузочных работ
switch:
  - platform: template
    name: "Автозакрытие"
    id: autoclose
    restore_mode: DISABLED
#    optimistic: true
    lambda: |-
      if (nicebust4 -> NiceBusT4::autocls_flag) {
        return true;
      } else {
        return false;
      }
    turn_on_action:
      lambda: |-
        nicebust4 -> NiceBusT4::send_inf_cmd("0003", "04", "80", "a9", "00", true, "01");
        nicebust4 -> NiceBusT4::send_inf_cmd("0003", "04", "84", "a9", "00", true, "01");
        nicebust4 -> NiceBusT4::send_inf_cmd("0003", "04", "80", "99", "00", true, "01");
        nicebust4 -> NiceBusT4::send_inf_cmd("0003", "04", "84", "99", "00", true, "01");                
    turn_off_action:
      lambda: |-
        nicebust4 -> NiceBusT4::send_inf_cmd("0003", "04", "80", "a9", "00", true, "00");
        nicebust4 -> NiceBusT4::send_inf_cmd("0003", "04", "84", "a9", "00", true, "00");
        nicebust4 -> NiceBusT4::send_inf_cmd("0003", "04", "80", "99", "00", true, "01");
        nicebust4 -> NiceBusT4::send_inf_cmd("0003", "04", "84", "99", "00", true, "01");                        

              
#script:
#  - id: send_cmd
#    then:
#      - switch.turn_on: my_switch
#      - delay: 1s
#      - switch.turn_off: my_switch              

# Кнопки для отправки команд
button:
  - platform: template
    name: Пошагово
    id: sbs
    on_press:
      lambda: |-
           nicebust4 -> NiceBusT4::send_cmd(nice_bust4::SBS);

  - platform: template
    name: Статус входов
    id: in_stat
    on_press:
      lambda: |-
           nicebust4 -> NiceBusT4::send_raw_cmd("55.0D.00.03.00.66.08.06.6B.04.D0.99.00.00.4D.0D");

#         nicebust4 -> NiceBusT4::send_raw_cmd("55 0c 00 ff 00 66 01 05 9D 01 82 01 64 E6 0c");
# 55.0E.00.03.00.81.08.07.8D.04.0B.A9.00.01.01.A6.0E поиск положений

  - platform: template
    name: Частичное открытие 1
    id: p_opn1
    on_press:
      lambda: |-
         nicebust4 -> NiceBusT4::send_cmd(nice_bust4::P_OPN1);
                      
#         nicebust4 -> NiceBusT4::send_cmd(nice_bust4::STOP);
#         nicebust4 -> NiceBusT4::send_cmd(nice_bust4::OPEN);
#         nicebust4 -> NiceBusT4::send_cmd(nice_bust4::CLOSE);
#         nicebust4 -> NiceBusT4::send_cmd(nice_bust4::P_OPN2);
#         nicebust4 -> NiceBusT4::send_cmd(nice_bust4::P_OPN3);
#         nicebust4 -> NiceBusT4::send_cmd(nice_bust4::P_OPN4);
#         nicebust4 -> NiceBusT4::send_cmd(nice_bust4::P_OPN5);
#         nicebust4 -> NiceBusT4::send_cmd(nice_bust4::P_OPN6);
#         nicebust4 -> NiceBusT4::send_cmd(nice_bust4::UNLK_OPN);  # Разблокировать и открыть 
#         nicebust4 -> NiceBusT4::send_cmd(nice_bust4::CLS_LOCK);  # Закрыть и блокировать
#         nicebust4 -> NiceBusT4::send_cmd(nice_bust4::UNLCK_CLS); # Разблокировать и Закрыть
#         nicebust4 -> NiceBusT4::send_cmd(nice_bust4::LOCK);      # Блокировать
#         nicebust4 -> NiceBusT4::send_cmd(nice_bust4::UNLOCK);    # Разблокировать
#         nicebust4 -> NiceBusT4::send_cmd(nice_bust4::HOST_SBS);  # Ведущий SBS
#         nicebust4 -> NiceBusT4::send_cmd(nice_bust4::HOST_OPN);  # Ведущий открыть
#         nicebust4 -> NiceBusT4::send_cmd(nice_bust4::HOST_CLS);  # Ведущий закрыть
#         nicebust4 -> NiceBusT4::send_cmd(nice_bust4::SLAVE_SBS); # Ведомый SBS
#         nicebust4 -> NiceBusT4::send_cmd(nice_bust4::SLAVE_OPN); # Ведомый открыть
#         nicebust4 -> NiceBusT4::send_cmd(nice_bust4::SLAVE_CLS); # Ведомый закрыть
#         nicebust4 -> NiceBusT4::send_cmd(nice_bust4::AUTO_ON);   # Автооткрывание активно
#         nicebust4 -> NiceBusT4::send_cmd(nice_bust4::AUTO_OFF);  # Автооткрывание неактивно
