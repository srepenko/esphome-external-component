#include "cover.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"  // для использования вспомогательных функция работ со строками
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace nice_bust4 {

static const char *TAG = "NiceBusT4Cover";

using namespace esphome::cover;


void NiceBusT4Cover::control(const CoverCall &call) {

}

void NiceBusT4Cover::setup() {
}

void NiceBusT4Cover::loop() {
} //loop

void NiceBusT4Cover::dump_config() {
}

}  // namespace nice_bust4
}  // namespace esphome