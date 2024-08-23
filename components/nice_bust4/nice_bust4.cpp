#include "esphome/core/log.h"
#include "nice_bust4.h"
#include "esphome/core/helpers.h"  // для использования вспомогательных функция работ со строками

namespace esphome {
namespace nice_bust4 {

static const char *TAG = "nice_bust4.component";

void NiceBusT4::setup() {

}

void NiceBusT4::loop() {
    
}

void NiceBusT4::dump_config(){
    ESP_LOGCONFIG(TAG, "NiceBusT4");
}




}  // namespace nice_bust4
}  // namespace esphome