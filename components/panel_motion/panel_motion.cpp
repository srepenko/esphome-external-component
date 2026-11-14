#include "panel_motion.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#ifdef USE_LOGGER
#include "esphome/components/logger/logger.h"
#endif

namespace esphome::panel_motion {

// Tessera's camera icon.
static constexpr uint32_t ICON_CAMERA = 0xF07AE;   // cctv
// A picture keeps the item in the top bar this long: Home Assistant's dashboards ask for a new one every ten seconds.
static constexpr uint32_t PICTURE_SHOWN_MS = 12000;
// A stream is live while its pictures come; at five a second, a gap this long means it ended.
static constexpr uint32_t STREAM_QUIET_MS = 3000;
static const char *const TAG = "panel_motion";

// The eye in the top bar while sensing runs.
static constexpr uint32_t ICON_EYE = 0xF0208;   // eye
// Sensing reads a frame at most this often; the camera delivers about two a second.
static constexpr uint32_t SAMPLE_MS = 400;
// A cell counts as changed when it moved more than this (0..255, after the frame's own shift in brightness is taken
// off), plus a little more as gain, and with it noise, rises.
static constexpr int MOTION_CELL_THRESHOLD = 5;   // панель: темний коридор, поріг нижче
// Motion: at least this many of the 576 cells changed between two samples.
static constexpr int MOTION_CELLS = 4;
// Exposure x gain moving more than this between two samples is auto exposure at work, not motion.
static constexpr float EXPOSURE_STEP = 0.30f;   // панель: у півтемряві AE постійно підлаштовується
// 40% of the cells or more changing the same way at once (all brighter, or all darker) is a light switched on or off,
// not someone moving: a lamp lights the room unevenly, a person changes one part of it, some cells brighter and some
// darker.
static constexpr int LIGHT_CHANGE_CELLS = 32 * 18 * 2 / 5;
// After a light change auto exposure, white balance and the gamma (which follows the scene's brightness) take a few
// seconds to settle, and every frame of it looks different: no motion is judged for this long (a light switched on gave
// motion).
static constexpr uint32_t SETTLE_MS = 3000;
// Motion stays on this long after the last changed sample (plugin.yaml adds its own hold for Home Assistant).
static constexpr uint32_t MOTION_ON_MS = 2500;
// The light level goes to Home Assistant at most this often, unless it changed a lot.
static constexpr uint32_t LUX_PUBLISH_MS = 10000;
// The light level from the frame's mean brightness, exposure time and gain (the sensor's, from ov02c10_sensor.c):
// lux = LUX_SCALE * linear mean / (exposure in seconds * gain), with the ISP's gamma (about 0.55) taken off the mean
// first. LUX_SCALE is the exposure equation for an f/2 lens and a sensor of about ISO 100 at gain 1: a rough figure,
// right in proportion, which is what dimming by it needs.
static constexpr float LUX_SCALE = 48.5f;
static constexpr float INVERSE_GAMMA = 1.8f;

extern "C" void ov02c10_sensor_exposure(uint32_t *lines, uint32_t *gain_x16, uint32_t *tline_ns);

// The parts of the camera that speak in ESP-IDF's log (forwarded by ESPHome under the tag "esp-idf"): the sensor
// driver, Espressif's video pipeline, its image processor and the camera bus.
static const char *const CAMERA_TAGS[] = {"ov02c10", "esp_video", "isp", "ipa", "cam_sensor", "sccb", "csi", "cam_ctlr"};

void PanelMotion::listen_to_log() {
#if defined(USE_LOGGER) && defined(USE_LOG_LISTENERS)
  if (logger::global_logger != nullptr) {
    logger::global_logger->add_log_callback(
        this, [](void *self, uint8_t level, const char *tag, const char *message, size_t length) {
          static_cast<PanelMotion *>(self)->on_log_(level, tag, message, length);
        });
  }
#endif
}

void PanelMotion::setup() {
  if (camera_ != nullptr) camera_->add_listener(this);
  set_interval("tick", 1000, [this]() { this->on_interval(millis()); });
  set_up_ = true;
  apply_sensing_();
  publish_status_();
  publish_activity_();
}

void PanelMotion::set_sensing(bool on) {
  sensing_wanted_ = on;
  apply_sensing_();
}

// Starts or stops the camera's frames for sensing. While they run the camera stays on, at the frame rate plugin.yaml
// sets, and its auto exposure stays settled, so a picture Home Assistant asks for comes at once; nothing is encoded or
// sent for sensing.
void PanelMotion::apply_sensing_() {
  if (!set_up_ || camera_ == nullptr || camera_->is_failed()) return;
  bool on = sensing_wanted_;
  if (on == sensing_on_) return;
  auto *video = video_();
  if (on) {
    grid_valid_ = false;
    old_valid_ = false;
    light_level_ = -1.0f;
    last_exposure_product_ = 0;
    settle_until_ms_ = millis() + SETTLE_MS;
    if (!video->register_raw_frame_consumer(this, esp_video_camera::RawVideoPixelFormat::RGB565_LE) ||
        !video->start_raw_frame_consumer(this)) {
      ESP_LOGW(TAG, "The camera does not hand out its frames; no light level or motion");
      return;
    }
    ESP_LOGI(TAG, "Sensing light and motion");
  } else {
    video->stop_raw_frame_consumer(this);
    ESP_LOGI(TAG, "Sensing stopped");
  }
  sensing_on_ = on;
  publish_sensing_();
  publish_activity_();
}

void PanelMotion::consume_raw_video_frame(const esp_video_camera::RawVideoFrame &frame) {
  const uint32_t now = millis();
  if (last_sample_ms_ != 0 && now - last_sample_ms_ < SAMPLE_MS) return;
  if (frame.pixel_format != esp_video_camera::RawVideoPixelFormat::RGB565_LE || frame.width < GRID_W * 4 ||
      frame.height < GRID_H * 4 || frame.data == nullptr) {
    return;
  }
  last_sample_ms_ = now;

  // The grid: each cell the mean brightness (0..255) of 4 x 4 points spread over it.
  uint8_t grid[GRID_W * GRID_H];
  uint32_t total = 0;
  const int cell_w = frame.width / GRID_W, cell_h = frame.height / GRID_H;
  for (int gy = 0; gy < GRID_H; gy++) {
    for (int gx = 0; gx < GRID_W; gx++) {
      uint32_t sum = 0;
      for (int sy = 0; sy < 4; sy++) {
        const int y = gy * cell_h + (2 * sy + 1) * cell_h / 8;
        const auto *row = reinterpret_cast<const uint16_t *>(frame.data + (size_t) y * frame.stride_bytes);
        for (int sx = 0; sx < 4; sx++) {
          const uint16_t p = row[gx * cell_w + (2 * sx + 1) * cell_w / 8];
          const uint32_t r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
          // BT.601 luma from 5-6-5 bits: R and B x 8, G x 4 to 0..255.
          sum += (r * 8 * 77 + g * 4 * 150 + b * 8 * 29) >> 8;
        }
      }
      const uint8_t cell = static_cast<uint8_t>(sum / 16);
      grid[gy * GRID_W + gx] = cell;
      total += cell;
    }
  }
  const float mean = static_cast<float>(total) / (GRID_W * GRID_H);

  uint32_t lines = 0, gain_x16 = 0, tline_ns = 0;
  ov02c10_sensor_exposure(&lines, &gain_x16, &tline_ns);
  const uint32_t product = lines * gain_x16;

  // A light change: auto exposure moving, or most of the frame brighter or darker at once (below). Then the camera
  // settles first.
  const bool exposure_steady =
      last_exposure_product_ != 0 && product != 0 &&
      std::fabs(static_cast<float>(product) - static_cast<float>(last_exposure_product_)) <=
          EXPOSURE_STEP * static_cast<float>(last_exposure_product_);
  if (!exposure_steady) settle_until_ms_ = now + SETTLE_MS;
  const bool settled = static_cast<int32_t>(now - settle_until_ms_) >= 0;

  // Motion: cells that changed by more than the whole frame did.
  if (grid_valid_ && old_valid_ && settled) {
    // у темряві підсилення велике: обмежуємо його внесок, інакше рух не помічається
    const int threshold = MOTION_CELL_THRESHOLD + std::min(static_cast<int>(gain_x16 / 64), 3);
    int shift = 0, brighter = 0, darker = 0;
    for (int i = 0; i < GRID_W * GRID_H; i++) {
      const int d = grid[i] - grid_old_[i];
      shift += d;
      if (d > threshold) brighter++;
      if (d < -threshold) darker++;
    }
    shift /= GRID_W * GRID_H;
    int changed = 0, bx0 = GRID_W, by0 = GRID_H, bx1 = -1, by1 = -1;
    for (int i = 0; i < GRID_W * GRID_H; i++) {
      if (std::abs(grid[i] - grid_old_[i] - shift) > threshold) {
        changed++;
        const int cx = i % GRID_W, cy = i / GRID_W;
        bx0 = std::min(bx0, cx); bx1 = std::max(bx1, cx); by0 = std::min(by0, cy); by1 = std::max(by1, cy);
      }
    }
    if (brighter >= LIGHT_CHANGE_CELLS || darker >= LIGHT_CHANGE_CELLS) {
      settle_until_ms_ = now + SETTLE_MS;
    } else {
      // поріг за освітленістю: у темряві (велике підсилення) людина змінює кадр слабше
      // поріг розміру руху (налаштовується з HA): людина біля панелі займає велику частину кадру
      const int need = this->min_cells_;
      static int streak = 0;
      if (changed >= need) {
        // рух рахуємо лише якщо він є на двох вибірках поспіль — відсікає шум і поодинокі мерехтіння
        // рух рахуємо лише якщо він є на трьох вибірках поспіль (~1.5 с) — відсікає шум, відблиски й мерехтіння
        if (++streak >= 3) {
          if (streak == 3)
            ESP_LOGI(TAG, "MOTION: cells %d (need %d), area x%d-%d y%d-%d of %dx%d, shift %d, gain_x16 %u", changed, need,
                     bx0, bx1, by0, by1, GRID_W, GRID_H, shift, (unsigned) gain_x16);
          last_motion_ms_.store(now == 0 ? 1 : now);
        }
      } else {
        streak = 0;
      }
    }
    if (changed >= 2) ESP_LOGI(TAG, "changed cells %d (threshold %d, gain_x16 %u)", changed, threshold, (unsigned) gain_x16);
  }
  {
    // діагностика раз на 5 с: чи стабільна експозиція і наскільки змінюється кадр
    static uint32_t last_diag = 0;
    if (now - last_diag > 5000) {
      last_diag = now;
      int maxd = 0;
      if (grid_valid_) for (int i = 0; i < GRID_W * GRID_H; i++) maxd = std::max(maxd, std::abs(grid[i] - grid_[i]));
      ESP_LOGI(TAG, "diag: mean %.0f exp %u gain_x16 %u steady %d settled %d maxdiff %d", mean, (unsigned) lines,
               (unsigned) gain_x16, (int) exposure_steady, (int) settled, maxd);
    }
  }
  if (grid_valid_) { memcpy(grid_old_, grid_, sizeof(grid_old_)); old_valid_ = true; }
  memcpy(grid_, grid, sizeof(grid_));
  grid_valid_ = true;
  last_exposure_product_ = product;

  // Light level.
  if (lines > 0 && gain_x16 > 0 && tline_ns > 0) {
    const float seconds = lines * (tline_ns * 1e-9f);
    const float gain = gain_x16 / 16.0f;
    const float linear = std::pow(mean / 255.0f, INVERSE_GAMMA);
    const float lux = LUX_SCALE * linear / (seconds * gain);
    light_level_ = light_level_ < 0 ? lux : 0.7f * light_level_ + 0.3f * lux;
    lux_x10_.store(static_cast<uint32_t>(light_level_ * 10.0f + 0.5f));
  }
  sensed_frames_.fetch_add(1);
  enable_loop_soon_any_context();
}

void PanelMotion::loop() {
  disable_loop();
  publish_sensing_();
}

void PanelMotion::publish_sensing_() {
  const uint32_t now = millis();
#ifdef USE_BINARY_SENSOR
  if (motion_ != nullptr) {
    const uint32_t last = last_motion_ms_.load();
    const bool moving = sensing_on_ && last != 0 && now - last < MOTION_ON_MS;
    if (moving != motion_shown_ || !motion_->has_state()) {
      motion_shown_ = moving;
      motion_->publish_state(moving);
    }
  }
#endif
#ifdef USE_SENSOR
  if (illuminance_ != nullptr && sensing_on_) {
    const uint32_t lux_x10 = lux_x10_.load();
    if (lux_x10 != UINT32_MAX) {
      // At most every ten seconds, at once when it changed by a quarter (a light switched on or off).
      const float last = static_cast<float>(published_lux_x10_);
      const bool big = published_lux_x10_ == UINT32_MAX || std::fabs(static_cast<float>(lux_x10) - last) > 0.25f * last + 5;
      if (big || (now - last_lux_publish_ms_ >= LUX_PUBLISH_MS && lux_x10 != published_lux_x10_)) {
        published_lux_x10_ = lux_x10;
        last_lux_publish_ms_ = now;
        illuminance_->publish_state(lux_x10 / 10.0f);
      }
    }
  }
#endif
}

// Whether ESP-IDF wrote this line at this level: its letter before the time, "E (1234)". The message need not end in a
// NUL, so this looks only within its length.
static bool idf_level(const char *message, size_t length, char letter) {
  for (size_t i = 0; i + 2 < length; i++) {
    if (message[i] == letter && message[i + 1] == ' ' && message[i + 2] == '(') return true;
  }
  return false;
}

// A line of the log without its colours and its header: "[E][esp_video_camera:445]: esp_video_init() failed: ..."
// becomes "esp_video_init() failed: ...", and ESP-IDF's "E (1234) ov02c10: Get sensor ID failed" becomes
// "ov02c10: Get sensor ID failed".
static size_t clean_line(const char *message, size_t length, char *out, size_t room) {
  size_t n = 0;
  for (size_t i = 0; i < length && n + 1 < room; i++) {
    if (message[i] == '\033') {   // a colour: ESC [ ... m
      while (i < length && message[i] != 'm') i++;
      continue;
    }
    if (message[i] == '\r' || message[i] == '\n') continue;
    out[n++] = message[i];
  }
  out[n] = '\0';
  const char *start = out;
  if (const char *header = strstr(start, "]: ")) start = header + 3;
  // ESP-IDF's own level and time: "E (1234) ".
  if ((start[0] == 'E' || start[0] == 'W' || start[0] == 'I') && start[1] == ' ' && start[2] == '(') {
    if (const char *after = strstr(start, ") ")) start = after + 2;
  }
  n = strlen(start);
  memmove(out, start, n + 1);
  return n;
}

void PanelMotion::on_log_(uint8_t level, const char *tag, const char *message, size_t length) {
  if (said_len_ + 8 >= sizeof(said_)) return;   // full: the first lines are the ones that tell why
  if (strcmp(tag, "esp_video_camera") == 0) {
    if (level <= ESPHOME_LOG_LEVEL_WARN) keep_line_(message, length);
    return;
  }
  if (strcmp(tag, "esp-idf") != 0) return;
  char line[160];
  size_t n = clean_line(message, length, line, sizeof(line));
  if (n == 0) return;
  bool camera = false;
  for (const char *name : CAMERA_TAGS) {
    if (strncmp(line, name, strlen(name)) == 0) camera = true;
  }
  if (!camera) return;
  bool trouble = idf_level(message, length, 'E') || idf_level(message, length, 'W');
  bool found = strstr(line, "Detected Camera sensor") != nullptr;
  if (trouble || found) keep_line_(message, length);
}

void PanelMotion::keep_line_(const char *message, size_t length) {
  char line[160];
  size_t n = clean_line(message, length, line, sizeof(line));
  if (n == 0) return;
  if (said_len_ > 0 && said_len_ + 3 < sizeof(said_)) {
    memcpy(said_ + said_len_, " | ", 3);
    said_len_ += 3;
  }
  size_t room = sizeof(said_) - 1 - said_len_;
  if (n > room) n = room;
  memcpy(said_ + said_len_, line, n);
  said_len_ += n;
  said_[said_len_] = '\0';
}

void PanelMotion::publish_status_() {
#ifdef USE_TEXT_SENSOR
  if (status_ == nullptr || camera_ == nullptr) return;
  char status[sizeof(shown_status_)];
  const char *state = camera_->is_failed()                       ? "Failed"
                      : pictured_ || sensed_frames_.load() > 0 ? "Working"
                                                               : "Started, no picture asked for yet";
  if (said_len_ > 0) {
    snprintf(status, sizeof(status), "%s: %s", state, said_);
  } else if (camera_->is_failed()) {
    snprintf(status, sizeof(status), "%s, without a reason in the log", state);
  } else {
    snprintf(status, sizeof(status), "%s", state);
  }
  if (strcmp(status, shown_status_) == 0) return;
  memcpy(shown_status_, status, sizeof(shown_status_));
  if (camera_->is_failed()) ESP_LOGE(TAG, "Camera status: %s", status);
  status_->publish_state(status);
#endif
}

// What the camera does now, as the top bar shows it (GitHub issue #1): a stream (Live), a picture now and then
// (Picture), light and motion only (Sensing), nothing (Idle), or Failed.
void PanelMotion::publish_activity_() {
#ifdef USE_TEXT_SENSOR
  if (activity_ == nullptr || camera_ == nullptr) return;
  const char *activity = camera_->is_failed() ? "Failed"
                         : live()             ? "Live"
                         : watching()         ? "Picture"
                         : sensing_on_        ? "Sensing"
                                              : "Idle";
  if (activity == shown_activity_) return;
  shown_activity_ = activity;
  activity_->publish_state(activity);
#endif
}

bool PanelMotion::watching() const {
  return last_picture_ != 0 && millis() - last_picture_ < PICTURE_SHOWN_MS;
}

bool PanelMotion::live() const {
  return streaming_ && millis() - last_stream_ < STREAM_QUIET_MS;
}

void PanelMotion::on_camera_image(const std::shared_ptr<camera::CameraImage> &image) {
  last_picture_ = millis();
  if (last_picture_ == 0) last_picture_ = 1;
  last_stream_ = last_picture_;
  pictured_ = true;
  if (!shown_) {
    shown_ = true;
  }
  publish_activity_();
}

void PanelMotion::on_stream_start() {
  if (!streaming_) {
    ESP_LOGD(TAG, "Stream started");
  }
  streaming_ = true;
  last_stream_ = millis();
  publish_activity_();
}

void PanelMotion::on_stream_stop() {
  if (streaming_) {
    ESP_LOGD(TAG, "Stream stopped");
  }
  streaming_ = false;
  publish_activity_();
}

void PanelMotion::on_interval(uint32_t now_ms) {
  // The item goes as soon as the last picture is old, not at the bar's next look; a stream that went quiet is no
  // longer live.
  if (shown_ && !watching()) {
    shown_ = false;
  }
  if (streaming_ && !live()) {
    ESP_LOGD(TAG, "Stream went quiet");
    streaming_ = false;
  }
  publish_status_();
  publish_sensing_();
  publish_activity_();
}

}  // namespace esphome::panel_motion
