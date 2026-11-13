#include "mjpeg_stream.h"
#include "esp_cache.h"
#ifdef USE_LVGL
#include "lvgl.h"
#endif
#include "esp_heap_caps.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esp_heap_caps.h"
#include <cstring>
#include <algorithm>
#include <cstdlib>

#ifdef USE_LVGL
#include "esphome/components/lvgl/lvgl_proxy.h"
#endif

namespace esphome::mjpeg_stream {

static const char *const TAG = "mjpeg_stream";

// діагностика: скільки внутрішньої пам'яті лишилось після глобальних конструкторів (перед стартом задач)
__attribute__((constructor(65535))) static void heap_probe_late() {
  ESP_EARLY_LOGW("heapprobe", "late ctor: internal free %u, largest %u, dma free %u",
                 (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                 (unsigned) heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
}
__attribute__((constructor(101))) static void heap_probe_early() {
  ESP_EARLY_LOGW("heapprobe", "early ctor: internal free %u", (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

MjpegStream::MjpegStream(int width, int height)
    : image::Image(nullptr, width, height, image::IMAGE_TYPE_RGB565, image::TRANSPARENCY_OPAQUE) {
  // буфери виділяються в setup(): виділення в конструкторі (до старту задач) ламало запуск
  this->frame_bytes_ = (size_t) width * height * 2;
}

void MjpegStream::setup() {
  {
    jpeg_decode_memory_alloc_cfg_t out_cfg = {};
    out_cfg.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER;
    size_t alloc_a = 0, alloc_b = 0;
    this->buf_a_ = (uint8_t *) jpeg_alloc_decoder_mem(this->frame_bytes_, &out_cfg, &alloc_a);
    this->buf_b_ = (uint8_t *) jpeg_alloc_decoder_mem(this->frame_bytes_, &out_cfg, &alloc_b);
    if (this->buf_a_ && this->buf_b_) {
      this->out_alloc_ = std::min(alloc_a, alloc_b);
      memset(this->buf_a_, 0, this->frame_bytes_);
      memset(this->buf_b_, 0, this->frame_bytes_);
      this->front_ = this->buf_a_;
      this->back_ = this->buf_b_;
      this->data_start_ = this->front_;
    }
  }
#ifdef USE_LVGL
  // опис картинки для LVGL заповнюється при першому зверненні; тоді буфера ще не було — заповнюємо заново
  this->dsc_.data = nullptr;
  this->get_lv_image_dsc();
#endif
  jpeg_decode_engine_cfg_t eng_cfg = {};
  eng_cfg.intr_priority = 0;
  eng_cfg.timeout_ms = 200;
  if (jpeg_new_decoder_engine(&eng_cfg, &this->decoder_) != ESP_OK) {
    ESP_LOGE(TAG, "Не вдалося створити апаратний JPEG-декодер");
    this->mark_failed();
    return;
  }

  jpeg_decode_memory_alloc_cfg_t in_cfg = {};
  in_cfg.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER;
  this->jpeg_buf_ = (uint8_t *) jpeg_alloc_decoder_mem(this->jpeg_cap_, &in_cfg, &this->jpeg_alloc_);
  if (!this->buf_a_ || !this->buf_b_ || !this->jpeg_buf_) {
    ESP_LOGE(TAG, "Не вистачає пам'яті для буферів %dx%d", this->width_, this->height_);
    this->mark_failed();
    return;
  }
  this->lock_ = xSemaphoreCreateMutex();
  if (this->disp_ != nullptr) {
    ppa_client_config_t pc = {};
    pc.oper_type = PPA_OPERATION_SRM;
    if (ppa_register_client(&pc, &this->ppa_) != ESP_OK) {
      ESP_LOGE(TAG, "PPA недоступний — пряме виведення вимкнено");
      this->ppa_ = nullptr;
    }
    this->rot_size_ = (this->frame_bytes_ + 63) & ~((size_t) 63);
    this->rot_ = (uint8_t *) heap_caps_aligned_calloc(64, 1, this->rot_size_, MALLOC_CAP_SPIRAM);
    this->tmp_ = (uint8_t *) heap_caps_aligned_calloc(64, 1, this->rot_size_, MALLOC_CAP_SPIRAM);
    if (this->rot_ == nullptr) {
      ESP_LOGE(TAG, "Не вистачає пам'яті для буфера повороту");
      this->ppa_ = nullptr;
    }
  }
  this->ready_ = true;

  // start() міг бути викликаний раніше (подія завантаження сторінки LVGL) — виконуємо зараз
  if (this->autostart_ || this->pending_start_)
    this->start();
}

void MjpegStream::dump_config() {
  ESP_LOGCONFIG(TAG, "MJPEG stream:\n  URL: %s\n  Розмір: %dx%d\n  Порядок кольорів: %s", this->url_.c_str(),
                this->width_, this->height_, this->bgr_ ? "BGR" : "RGB");
}

void MjpegStream::start() {
  if (!this->ready_) {  // ще до setup(): м'ютекс і декодер не створені
    this->pending_start_ = true;
    return;
  }
  if (this->is_failed() || this->running_.load())
    return;
  this->running_.store(true);
  if (this->task_ == nullptr) {
    // окрема задача, щоб мережеві очікування не блокували основний цикл
    xTaskCreatePinnedToCore(MjpegStream::task_entry_, "mjpeg", 6144, this, 4, &this->task_, 0);
  }
  ESP_LOGI(TAG, "Потік запущено");
}

void MjpegStream::stop() {
  this->pending_start_ = false;
  this->running_.store(false);
  ESP_LOGI(TAG, "Потік зупинено");
}

#ifdef USE_LVGL
// Після перемальовки LVGL у зоні відео (фон чорного «вікна») одразу повертаємо останній кадр — без чорних блимань.
static std::vector<MjpegStream *> g_direct;
static bool g_hook = false;
static void lv_redraw_cb(lv_event_t *e) {
  static bool dirty[8] = {};
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_INVALIDATE_AREA) {
    const lv_area_t *a = (const lv_area_t *) lv_event_get_param(e);
    if (a == nullptr) return;
    for (size_t i = 0; i < g_direct.size() && i < 8; i++) {
      auto *s = g_direct[i];
      if (!s->direct_active()) continue;
      int x1 = s->get_dx(), y1 = s->get_dy(), x2 = x1 + s->get_width() - 1, y2 = y1 + s->get_height() - 1;
      if (a->x2 >= x1 && a->x1 <= x2 && a->y2 >= y1 && a->y1 <= y2 && !s->inside_exclude(a->x1, a->y1, a->x2, a->y2))
        dirty[i] = true;
    }
  } else if (code == LV_EVENT_REFR_READY) {
    for (size_t i = 0; i < g_direct.size() && i < 8; i++) {
      if (dirty[i] && g_direct[i]->direct_active()) g_direct[i]->blit_now();
      dirty[i] = false;
    }
  }
}
#endif

void MjpegStream::blit_() {
  if (this->disp_ == nullptr || this->ppa_ == nullptr || this->front_ == nullptr)
    return;
  const int w = this->width_, h = this->height_;
  uint16_t *px = (uint16_t *) this->front_;
  if (this->radius_ > 0) {
    // кути кольором фону (як у карток інтерфейсу)
    const int r = this->radius_;
    for (int yy = 0; yy < r; yy++) {
      for (int xx = 0; xx < r; xx++) {
        float fx = r - xx - 0.5f, fy = r - yy - 0.5f;
        if (fx * fx + fy * fy <= (float) (r * r))
          continue;
        px[yy * w + xx] = this->bg565_;
        px[yy * w + (w - 1 - xx)] = this->bg565_;
        px[(h - 1 - yy) * w + xx] = this->bg565_;
        px[(h - 1 - yy) * w + (w - 1 - xx)] = this->bg565_;
      }
    }
    esp_cache_msync(this->front_, (this->frame_bytes_ + 63) & ~((size_t) 63),
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  }
  ppa_srm_oper_config_t c = {};
  c.in.buffer = this->front_;
  c.in.pic_w = w;
  c.in.pic_h = h;
  c.in.block_w = w;
  c.in.block_h = h;
  c.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
  c.out.buffer = this->rot_;
  c.out.buffer_size = this->rot_size_;
  c.out.pic_w = h;
  c.out.pic_h = w;
  c.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
  c.rotation_angle = PPA_SRM_ROTATION_ANGLE_270;  // 270° проти год. стрілки = 90° за годинниковою (як LVGL)
  c.scale_x = 1.0f;
  c.scale_y = 1.0f;
  c.mode = PPA_TRANS_MODE_BLOCKING;
  if (ppa_do_scale_rotate_mirror(this->ppa_, &c) != ESP_OK) {
    ESP_LOGW(TAG, "PPA: помилка повороту");
    return;
  }
  this->have_frame_ = true;
  // логічні (альбомні) координати -> фізичні (портретна панель), як у LVGL при rotation: 90.
  // Рядок повернутого буфера = стовпець x відео (довжина h), піксель рядка r відповідає y = h-1-r.
  const int phys_x = this->disp_->get_native_width() - this->dy_ - h;
  const int phys_y = this->dx_;
  if (this->excl_.empty()) {
    this->disp_->draw_pixels_at(phys_x, phys_y, h, w, this->rot_, display::COLOR_ORDER_RGB, display::COLOR_BITNESS_565,
                                false);
    return;
  }
  // для кожного стовпця x: видима частина відео — [top, bot) по y (виключення лише біля верхнього/нижнього краю)
  auto span = [&](int x, int &top, int &bot) {
    top = 0;
    bot = h;
    for (auto &r : this->excl_) {
      if (x < r.x || x >= r.x + r.w) continue;
      if (r.y == 0) top = std::max(top, r.h);
      else bot = std::min(bot, r.y);
    }
  };
  int x = 0;
  while (x < w) {
    int top, bot;
    span(x, top, bot);
    int x2 = x + 1;
    while (x2 < w) {
      int t2, b2;
      span(x2, t2, b2);
      if (t2 != top || b2 != bot) break;
      x2++;
    }
    const int cols = x2 - x, seg = bot - top;
    if (seg > 0) {
      const int r0 = h - bot;  // перший піксель у рядку повернутого буфера
      const uint16_t *src = (const uint16_t *) this->rot_ + (size_t) x * h;
      if (seg == h) {
        this->disp_->draw_pixels_at(phys_x, phys_y + x, h, cols, (const uint8_t *) src, display::COLOR_ORDER_RGB,
                                    display::COLOR_BITNESS_565, false);
      } else {
        uint16_t *dst = (uint16_t *) this->tmp_;
        for (int i = 0; i < cols; i++)
          memcpy(dst + (size_t) i * seg, src + (size_t) i * h + r0, seg * 2);
        esp_cache_msync(this->tmp_, (((size_t) cols * seg * 2) + 63) & ~((size_t) 63),
                        ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
        this->disp_->draw_pixels_at(phys_x + r0, phys_y + x, seg, cols, this->tmp_, display::COLOR_ORDER_RGB,
                                    display::COLOR_BITNESS_565, false);
      }
    }
    x = x2;
  }
}

void MjpegStream::loop() {
#ifdef USE_LVGL
  if (this->disp_ != nullptr && !this->hooked_) {
    lv_display_t *ld = lv_display_get_default();
    if (ld != nullptr) {
      this->hooked_ = true;
      g_direct.push_back(this);
      if (!g_hook) {
        g_hook = true;
        lv_display_add_event_cb(ld, lv_redraw_cb, LV_EVENT_ALL, nullptr);
      }
    }
  }
#endif
  // обмін буферів відбувається в основному циклі — там же, де LVGL малює
  if (this->frame_ready_.load() && xSemaphoreTake(this->lock_, 0) == pdTRUE) {
    std::swap(this->front_, this->back_);
    this->data_start_ = this->front_;
#ifdef USE_LVGL
    this->dsc_.data = this->front_;
#endif
    this->frame_ready_.store(false);
    xSemaphoreGive(this->lock_);
    if (this->direct_on_ && this->running_.load()) {
      this->blit_();
#ifdef USE_LVGL
      if (!this->excl_.empty() && (this->frames_ & 1) == 0 && lv_screen_active() != nullptr) {
        for (auto &r : this->excl_) {
          lv_area_t a = {(int32_t) (this->dx_ + r.x), (int32_t) (this->dy_ + r.y), (int32_t) (this->dx_ + r.x + r.w - 1),
                         (int32_t) (this->dy_ + r.y + r.h - 1)};
          lv_obj_invalidate_area(lv_screen_active(), &a);
        }
      }
#endif
    }
    this->frame_trigger_.trigger();

    this->frames_++;
    uint32_t now = millis();
    if (now - this->fps_window_start_ >= 5000) {
      this->fps_ = this->frames_ * 1000.0f / (now - this->fps_window_start_);
      ESP_LOGD(TAG, "%.1f кадр/с", this->fps_);
      this->frames_ = 0;
      this->fps_window_start_ = now;
    }
  }
  bool c = this->connected_.load();
  if (c != this->was_connected_) {
    this->was_connected_ = c;
    if (c)
      this->connected_trigger_.trigger();
    else
      this->disconnected_trigger_.trigger();
  }
}

void MjpegStream::task_entry_(void *arg) { static_cast<MjpegStream *>(arg)->run_(); }

void MjpegStream::run_() {
  while (true) {
    if (!this->running_.load()) {
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
    this->stream_once_();
    this->connected_.store(false);
    if (this->running_.load())
      vTaskDelay(pdMS_TO_TICKS(2000));  // пауза перед перепідключенням
  }
}

int MjpegStream::read_line_(esp_http_client_handle_t c, char *buf, int cap) {
  int n = 0;
  while (n < cap - 1) {
    char ch;
    int r = esp_http_client_read(c, &ch, 1);
    if (r <= 0)
      return -1;
    if (ch == '\n')
      break;
    if (ch != '\r')
      buf[n++] = ch;
  }
  buf[n] = 0;
  return n;
}

bool MjpegStream::read_exact_(esp_http_client_handle_t c, uint8_t *dst, int len) {
  int got = 0;
  while (got < len) {
    int r = esp_http_client_read(c, (char *) dst + got, len - got);
    if (r <= 0)
      return false;
    got += r;
  }
  return true;
}

bool MjpegStream::decode_(size_t jpeg_len) {
  jpeg_decode_cfg_t cfg = {};
  cfg.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
  cfg.rgb_order = this->bgr_ ? JPEG_DEC_RGB_ELEMENT_ORDER_BGR : JPEG_DEC_RGB_ELEMENT_ORDER_RGB;
  cfg.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;

  jpeg_decode_picture_info_t info = {};
  if (jpeg_decoder_get_info(this->jpeg_buf_, jpeg_len, &info) != ESP_OK)
    return false;
  if ((int) info.width != this->width_ || (int) info.height != this->height_) {
    ESP_LOGW(TAG, "Кадр %ux%u, очікувалось %dx%d — налаштуйте розмір у джерелі", (unsigned) info.width,
             (unsigned) info.height, this->width_, this->height_);
    return false;
  }
  xSemaphoreTake(this->lock_, portMAX_DELAY);
  uint32_t out_size = 0;
  esp_err_t err = jpeg_decoder_process(this->decoder_, &cfg, this->jpeg_buf_, jpeg_len, this->back_, this->out_alloc_,
                                       &out_size);
  if (err == ESP_OK)
    this->frame_ready_.store(true);
  xSemaphoreGive(this->lock_);
  return err == ESP_OK;
}

bool MjpegStream::stream_once_() {
  esp_http_client_config_t hc = {};
  hc.url = this->url_.c_str();
  hc.timeout_ms = 5000;
  hc.buffer_size = 4096;
  hc.disable_auto_redirect = false;
  if (!this->username_.empty()) {
    hc.username = this->username_.c_str();
    hc.password = this->password_.c_str();
    hc.auth_type = HTTP_AUTH_TYPE_BASIC;
  }
  esp_http_client_handle_t c = esp_http_client_init(&hc);
  if (c == nullptr)
    return false;
  bool ok = false;
  if (esp_http_client_open(c, 0) != ESP_OK) {
    ESP_LOGW(TAG, "Не вдалося підключитися до %s", this->url_.c_str());
  } else {
    esp_http_client_fetch_headers(c);
    int status = esp_http_client_get_status_code(c);
    if (status != 200) {
      ESP_LOGW(TAG, "HTTP %d від джерела", status);
    } else {
      ESP_LOGI(TAG, "Підключено до потоку");
      this->connected_.store(true);
      ok = true;
      char line[128];
      while (this->running_.load()) {
        // заголовки частини multipart: шукаємо Content-Length, порожній рядок — початок JPEG
        int content_len = -1;
        int n;
        bool header_seen = false;
        while ((n = this->read_line_(c, line, sizeof(line))) >= 0) {
          if (n == 0) {
            if (header_seen)
              break;
            continue;
          }
          header_seen = true;
          if (strncasecmp(line, "Content-Length:", 15) == 0)
            content_len = atoi(line + 15);
        }
        if (n < 0)
          break;  // з'єднання закрито
        if (content_len <= 0 || (size_t) content_len > this->jpeg_alloc_) {
          ESP_LOGW(TAG, "Неочікуваний розмір кадру: %d (буфер %u)", content_len, (unsigned) this->jpeg_alloc_);
          break;
        }
        if (!this->read_exact_(c, this->jpeg_buf_, content_len))
          break;
        // якщо основний цикл ще не забрав попередній кадр — замінюємо його новішим (мінімальна затримка)
        this->decode_(content_len);
      }
    }
  }
  esp_http_client_close(c);
  esp_http_client_cleanup(c);
  return ok;
}

}  // namespace esphome::mjpeg_stream
