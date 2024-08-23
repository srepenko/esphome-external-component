#pragma once

#include "esphome.h"
#include "esphome/core/component.h"
#include "esphome/core/automation.h"           // для добавления Action
#include "esphome/components/cover/cover.h"
#include "esphome/core/helpers.h"              // парсим строки встроенными инструментами




namespace esphome {
namespace nice_bust4 {
class NiceBusT4;
/* для короткого обращения к членам класса */
using namespace esphome::cover;


// создаю класс, наследую членов классов Component и Cover
class NiceBusT4Cover : public Component, public Cover{
  public:

    void setup() override;
    void loop() override;
    void dump_config() override; // для вывода в лог информации об оборудовнии
    void set_parent(NiceBusT4 *const parent) { this->parent_ = parent; }
    cover::CoverTraits get_traits() override;
    
  protected:
    void control(const cover::CoverCall &call) override;
    NiceBusT4 *parent_;
}; //класс

} // namespace nice_bust4
} // namespace esphome