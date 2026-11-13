#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/components/camera/camera.h"
#include "esphome/components/esp_video_camera/esp_video_camera.h"
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif

namespace esphome::panel_motion {

// The plugin: an item for the top bar that shows while pictures leave the screen's camera, in red while it streams. It listens to the camera as
// ESPHome's API does (camera::CameraListener), on the main loop: a picture, or a stream that starts or stops.
// And the camera's status for Home Assistant: the camera starts before Home Assistant connects, so what it said while it
// started (the sensor found or not, the image pipeline up or not) is kept from the log and shown as a text.
// And, while sensing is on, the room's light level and motion, worked out on the screen from the camera's frames: no
// picture leaves the screen for them. The camera hands its processed frames (RGB565) to this plugin on its own task
// (RawVideoFrameConsumer); the plugin reads a coarse grid of them and gives the results to the main loop.
class PanelMotion : public Component,
                     public camera::CameraListener,
                     public esp_video_camera::RawVideoFrameConsumer {
 public:
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::LATE; }
  void on_interval(uint32_t now_ms);

  void set_camera(camera::Camera *c) { camera_ = c; }
#ifdef USE_TEXT_SENSOR
  void set_status(text_sensor::TextSensor *s) { status_ = s; }
  void set_activity(text_sensor::TextSensor *s) { activity_ = s; }
#endif
#ifdef USE_SENSOR
  void set_illuminance(sensor::Sensor *s) { illuminance_ = s; }
#endif
#ifdef USE_BINARY_SENSOR
  void set_motion(binary_sensor::BinarySensor *s) { motion_ = s; }
#endif
  // Called while the firmware is made, before any component starts, so the camera's own start is in the log it keeps.
  void listen_to_log();
  // Sensing on or off (the setting "Camera motion and light"). Before setup() it is only remembered.
  void set_sensing(bool on);

  void on_camera_image(const std::shared_ptr<camera::CameraImage> &image) override;
  void on_stream_start() override;
  void on_stream_stop() override;
  // On the camera's task, for every frame while sensing is on.
  void consume_raw_video_frame(const esp_video_camera::RawVideoFrame &frame) override;

  // Whether someone is watching: a picture left the screen a moment ago. Live: a stream runs and its pictures still
  // come, so a stream that ends without a word from the API (a viewer that went away) goes quiet by itself.
  bool watching() const;
  bool live() const;

 protected:
  // The coarse grid sensing reads: cells of about 40 x 40 pixels, each the mean of 4 x 4 points.
  static constexpr int GRID_W = 32;
  static constexpr int GRID_H = 18;

  void on_log_(uint8_t level, const char *tag, const char *message, size_t length);
  void keep_line_(const char *message, size_t length);
  void publish_status_();
  void publish_activity_();
  void apply_sensing_();
  void publish_sensing_();

  esp_video_camera::ESPVideoCamera *video_() const {
    return static_cast<esp_video_camera::ESPVideoCamera *>(camera_);
  }

  camera::Camera *camera_{};
#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *status_{};
  text_sensor::TextSensor *activity_{};   // what the camera does now: Live, Picture, Sensing, Idle or Failed
  const char *shown_activity_{nullptr};
#endif
#ifdef USE_SENSOR
  sensor::Sensor *illuminance_{};
#endif
#ifdef USE_BINARY_SENSOR
  binary_sensor::BinarySensor *motion_{};
#endif
  bool streaming_{false};
  bool shown_{false};          // what the top bar shows now, so a change draws it at once
  bool pictured_{false};       // a picture left the camera since it started
  uint32_t last_picture_{0};   // millis() of the last picture, 0 before the first
  uint32_t last_stream_{0};    // millis() of the last sign of a stream: a request for it or one of its pictures
  // What the camera said while it started: the sensor's name and the first errors and warnings, " | " between them.
  char said_[200]{};
  size_t said_len_{0};
  char shown_status_[220]{};   // the status Home Assistant has, so only a change is sent

  // Sensing. Set up on the main loop; the camera's task owns the grid and the sample clock while it runs.
  bool sensing_wanted_{true};
  bool sensing_on_{false};
  bool set_up_{false};
  bool sensing_shown_{false};  // what the top bar shows for sensing now
  uint8_t grid_[GRID_W * GRID_H]{};
  bool grid_valid_{false};
  uint8_t grid_old_[GRID_W * GRID_H]{};
  bool old_valid_{false};
  uint32_t last_sample_ms_{0};
  uint32_t last_exposure_product_{0};
  uint32_t settle_until_ms_{0};     // no motion before this: the light changed and the camera is settling
  float light_level_{-1.0f};   // smoothed light level in lux, on the camera's task
  // What the camera's task found, for the main loop.
  std::atomic<uint32_t> sensed_frames_{0};
  std::atomic<uint32_t> last_motion_ms_{0};
  std::atomic<uint32_t> lux_x10_{UINT32_MAX};
  uint32_t published_lux_x10_{UINT32_MAX};
  uint32_t last_lux_publish_ms_{0};
  bool motion_shown_{false};
};

}  // namespace esphome::panel_motion
