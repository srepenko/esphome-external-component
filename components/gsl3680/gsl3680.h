#pragma once

#include "esphome/core/application.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/touchscreen/touchscreen.h"
#include "esphome/core/hal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "gsl3680_firmware.h"
#include "gsl_point_id.h"

namespace esphome {
namespace gsl3680 {

#define TOUCH_MAX_POINTS 5

constexpr static const char *const TAG = "touchscreen.gsl3680";

class GSL3680 : public touchscreen::Touchscreen, public i2c::I2CDevice {
    public:
        void setup() override;
        void update_touches() override;

        void set_interrupt_pin(InternalGPIOPin *pin) { this->interrupt_pin_ = pin; }
        void set_reset_pin(InternalGPIOPin *pin) { this->reset_pin_ = pin; }

    protected:
        InternalGPIOPin *interrupt_pin_{};
        InternalGPIOPin *reset_pin_{};
        // size_t width_ = 1280;
        // size_t height_ = 800;
        // esp_lcd_touch_handle_t tp_{};
        // esp_lcd_panel_io_handle_t tp_io_handle_{};

        // Сенсор читається в окремій задачі одразу по перериванню, а не з основного циклу:
        // коли цикл зайнятий (відео), короткий дотик інакше встигає закінчитись непоміченим.
        // Кожне натискання фіксується (down_pending_) і передається інтерфейсу щонайменше HOLD_MS.
        static void isr_(GSL3680 *self);
        static void task_fn_(void *arg);
        void poll_();
        TaskHandle_t task_{nullptr};
        volatile bool pressed_{false};
        volatile bool down_pending_{false};
        volatile uint16_t lx_{0}, ly_{0};
        uint32_t hold_until_{0};

        esphome::i2c::ErrorCode reset();
        esphome::i2c::ErrorCode init();
        esphome::i2c::ErrorCode read_configuration();
        esphome::i2c::ErrorCode clear_registers();
        esphome::i2c::ErrorCode load_firmware();
        esphome::i2c::ErrorCode start();
        esphome::i2c::ErrorCode read_ram();
};

}
}
