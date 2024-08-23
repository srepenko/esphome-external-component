#include "cover.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"  // для использования вспомогательных функция работ со строками

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

}

void NiceBusT4Cover::setup() {
}

void NiceBusT4Cover::loop() {
} //loop

void NiceBusT4Cover::dump_config() {
}

}  // namespace nice_bust4
}  // namespace esphome