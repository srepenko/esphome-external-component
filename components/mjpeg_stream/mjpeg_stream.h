#pragma once
/*
  mjpeg_stream — живий MJPEG-потік для ESP32-P4 з апаратним JPEG-декодером.
  Одне постійне HTTP-з'єднання (multipart/x-mixed-replace), кадри декодуються
  апаратним JPEG-кодеком P4 у RGB565 і показуються як звичайне image для LVGL.
  Потік працює в окремій задачі FreeRTOS і не блокує основний цикл ESPHome.
*/
#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/image/image.h"
#include "esphome/components/display/display.h"

#include <atomic>
#include <string>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/jpeg_decode.h"
#include "esp_http_client.h"
#include "driver/ppa.h"

namespace esphome::mjpeg_stream {

class MjpegStream : public Component, public image::Image {
 public:
  MjpegStream(int width, int height);

  void set_url(const std::string &url) { this->url_ = url; }
  void set_username(const std::string &u) { this->username_ = u; }
  void set_password(const std::string &p) { this->password_ = p; }
  void set_autostart(bool a) { this->autostart_ = a; }
  void set_rgb_order_bgr(bool bgr) { this->bgr_ = bgr; }
  void set_max_jpeg_size(size_t s) { this->jpeg_cap_ = s; }
  // Пряме виведення в кадровий буфер дисплея (повз LVGL): поворот 90° апаратним PPA.
  // x, y — логічні координати (альбомна орієнтація LVGL), radius — заокруглення кутів кольором фону.
  void set_direct(display::Display *d, int x, int y, int radius, uint16_t bg565) {
    this->disp_ = d; this->dx_ = x; this->dy_ = y; this->radius_ = radius; this->bg565_ = bg565;
  }
  void set_direct_enabled(bool e) { this->direct_on_ = e; }
  // прямокутник (відносно відео), який не перемальовується — там віджети LVGL поверх відео.
  // Має торкатися верхнього (y == 0) або нижнього (y + h == висота) краю відео.
  void add_exclude(int x, int y, int w, int h) { this->excl_.push_back({x, y, w, h, nullptr}); }
  // зона діє лише поки віджет-власник видимий (прихована плитка — відео малюється й там, без перемальовки LVGL)
  void set_exclude_owner(size_t i, void *lv_obj) { if (i < this->excl_.size()) this->excl_[i].owner = lv_obj; }
  // true — поворот PPA одразу в кадровий буфер дисплея; false — старий шлях (буфер повороту + draw_pixels_at)
  void set_fast(bool f) { this->fast_ = f; }
  void blit_now() { this->blit_(); }
  bool inside_exclude(int x1, int y1, int x2, int y2) const {
    for (auto &r : this->excl_) {
      if (!this->excl_active_(r)) continue;
      int ex = this->dx_ + r.x, ey = this->dy_ + r.y;
      if (x1 >= ex && y1 >= ey && x2 <= ex + r.w - 1 && y2 <= ey + r.h - 1) return true;
    }
    return false;
  }
  bool direct_active() const { return this->disp_ != nullptr && this->direct_on_ && this->running_.load(); }
  int get_dx() const { return this->dx_; }
  int get_dy() const { return this->dy_; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void start();
  void stop();
  bool is_running() const { return this->running_.load(); }
  float get_fps() const { return this->fps_; }

  Trigger<> *get_frame_trigger() { return &this->frame_trigger_; }
  Trigger<> *get_connected_trigger() { return &this->connected_trigger_; }
  Trigger<> *get_disconnected_trigger() { return &this->disconnected_trigger_; }

 protected:
  static void task_entry_(void *arg);
  void run_();
  bool stream_once_();                                   // одне з'єднання до помилки/зупинки
  int read_line_(esp_http_client_handle_t c, char *buf, int cap);
  bool read_exact_(esp_http_client_handle_t c, uint8_t *dst, int len);
  bool decode_(size_t jpeg_len);
  void blit_();
  display::Display *disp_{nullptr};
  int dx_{0}, dy_{0}, radius_{0};
  uint16_t bg565_{0};
  bool direct_on_{true};
  ppa_client_handle_t ppa_{nullptr};
  uint8_t *rot_{nullptr};
  uint8_t *tmp_{nullptr};
  struct Rect { int x, y, w, h; void *owner; };
  std::vector<Rect> excl_;
  bool excl_active_(const Rect &r) const;
  void blit_fb_();     // швидкий шлях: PPA -> кадровий буфер
  void blit_copy_();   // старий шлях
  void round_corners_();
  bool fast_{true};
  bool fb_tried_{false};
  uint8_t *fb_{nullptr};
  int fb_w_{0}, fb_h_{0};
  size_t fb_size_{0};
  uint32_t blit_us_{0}, blit_n_{0}, continue_skip_{0};
  bool have_frame_{false};
  bool hooked_{false};
  size_t rot_size_{0};

  std::string url_, username_, password_;
  bool ready_{false};
  bool pending_start_{false};
  bool autostart_{true};
  bool bgr_{true};
  size_t frame_bytes_{0};

  // буфери: back_ (сюди декодує задача) і front_ (його бачить LVGL); обмін — в loop()
  uint8_t *buf_a_{nullptr}, *buf_b_{nullptr};
  size_t out_alloc_{0};
  uint8_t *front_{nullptr}, *back_{nullptr};
  uint8_t *jpeg_buf_{nullptr};
  size_t jpeg_cap_{96 * 1024}, jpeg_alloc_{0};

  jpeg_decoder_handle_t decoder_{nullptr};
  TaskHandle_t task_{nullptr};
  SemaphoreHandle_t lock_{nullptr};
  std::atomic<bool> running_{false};
  std::atomic<bool> frame_ready_{false};
  std::atomic<bool> connected_{false};
  bool was_connected_{false};

  uint32_t frames_{0}, fps_window_start_{0};
  float fps_{0};

  Trigger<> frame_trigger_;
  Trigger<> connected_trigger_;
  Trigger<> disconnected_trigger_;
};

template<typename... Ts> class StartAction : public Action<Ts...>, public Parented<MjpegStream> {
 public:
  void play(const Ts &...x) override { this->parent_->start(); }
};
template<typename... Ts> class StopAction : public Action<Ts...>, public Parented<MjpegStream> {
 public:
  void play(const Ts &...x) override { this->parent_->stop(); }
};

}  // namespace esphome::mjpeg_stream
