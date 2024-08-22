#include "esphome/core/log.h"
#include "nice_bust4.h"
#include "esphome/components/uart/uart_component_esp_idf.h"

namespace esphome {
namespace nice_bust4 {

static const char *TAG = "nice_bust4.component";

void NiceBusT4::setup() {
    //load_settings(119200);
}

void NiceBusT4::loop() {

}

void NiceBusT4::dump_config(){
    ESP_LOGCONFIG(TAG, "NiceBusT4");
}

}  // namespace nice_bust4
}  // namespace esphome