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
#include "esp_timer.h"
#if __has_include("esphome/components/mipi_dsi/mipi_dsi.h")
#include "esphome/components/mipi_dsi/mipi_dsi.h"
#include "esp_lcd_mipi_dsi.h"
#define MJPEG_HAVE_DSI 1
#endif

namespace esphome::mjpeg_stream {

static const char *const TAG = "mjpeg_stream";

#ifdef MJPEG_HAVE_DSI
// handle_ панелі в MipiDsi захищений, а клас final: явна інстанціація шаблону ігнорує права доступу
namespace {
template<typename Tag, typename Tag::type M> struct DsiRob { friend typename Tag::type dsi_get(Tag) { return M; } };
struct DsiHandleTag {
  typedef esp_lcd_panel_handle_t mipi_dsi::MipiDsi::*type;
  friend type dsi_get(DsiHandleTag);
};
template struct DsiRob<DsiHandleTag, &mipi_dsi::MipiDsi::handle_>;
}  // namespace
#endif

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
    pc.max_pending_trans_num = 32;  // блоки кадру ставляться в чергу PPA без очікування
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
  // замір: скільки часу LVGL витрачає на перемальовку екрана
  static int64_t r0 = 0, racc = 0;
  static uint32_t rn = 0, rlog = 0;
  static uint32_t inv_n = 0, inv_max = 0;
  static uint64_t inv_px = 0;
  static lv_area_t inv_big = {};
  if (code == LV_EVENT_INVALIDATE_AREA) {
    const lv_area_t *ia = (const lv_area_t *) lv_event_get_param(e);
    if (ia != nullptr) {
      uint32_t px = (uint32_t) lv_area_get_width(ia) * lv_area_get_height(ia);
      inv_n++; inv_px += px;
      if (px > inv_max) { inv_max = px; inv_big = *ia; }
    }
  }
  if (code == LV_EVENT_REFR_START) { r0 = esp_timer_get_time(); return; }
  if (code == LV_EVENT_REFR_READY && r0) {
    racc += esp_timer_get_time() - r0; rn++; r0 = 0;
    uint32_t now = millis();
    if (now - rlog >= 5000) {
      ESP_LOGD(TAG, "LVGL: %u перемальовок за 5 с, %.2f мс у середньому, %.0f мс/с усього", (unsigned) rn,
               rn ? racc / 1000.0f / rn : 0.0f, racc / 1000.0f / ((now - rlog) / 1000.0f));
      ESP_LOGD(TAG, "LVGL: інвалідацій %u, %llu тис. пікс., найбільша %ux%u @%d,%d", (unsigned) inv_n,
               (unsigned long long) (inv_px / 1000), (unsigned) lv_area_get_width(&inv_big),
               (unsigned) lv_area_get_height(&inv_big), (int) inv_big.x1, (int) inv_big.y1);
      inv_n = 0; inv_px = 0; inv_max = 0;
      racc = 0; rn = 0; rlog = now;
    }
  }
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

bool MjpegStream::excl_active_(const Rect &r) const {
#ifdef USE_LVGL
  if (r.owner != nullptr && lv_obj_has_flag((lv_obj_t *) r.owner, LV_OBJ_FLAG_HIDDEN)) return false;
#endif
  return true;
}

// кути кольором фону (як у карток інтерфейсу). front_ записав DMA декодера: спершу скидаємо можливі застарілі
// рядки кешу (M2C), пишемо кути й повертаємо в пам'ять лише ці рядки (C2M), а не весь кадр
void MjpegStream::round_corners_() {
  const int w = this->width_, h = this->height_, r = this->radius_;
  if (r <= 0) return;
  uint16_t *px = (uint16_t *) this->front_;
  const size_t rowb = (size_t) w * 2;
  auto span_sync = [&](int y0, int rows, int dir) {
    uintptr_t a0 = (uintptr_t) (this->front_ + (size_t) y0 * rowb) & ~(uintptr_t) 63;
    uintptr_t a1 = ((uintptr_t) (this->front_ + (size_t) (y0 + rows) * rowb) + 63) & ~(uintptr_t) 63;
    uintptr_t lo = (uintptr_t) this->front_ & ~(uintptr_t) 63;
    uintptr_t hi = ((uintptr_t) this->front_ + this->out_alloc_) & ~(uintptr_t) 63;
    a0 = std::max(a0, lo);
    a1 = std::min(a1, hi);
    if (a1 > a0) esp_cache_msync((void *) a0, a1 - a0, dir);
  };
  span_sync(0, r, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
  span_sync(h - r, r, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
  for (int yy = 0; yy < r; yy++) {
    for (int xx = 0; xx < r; xx++) {
      float fx = r - xx - 0.5f, fy = r - yy - 0.5f;
      if (fx * fx + fy * fy <= (float) (r * r)) continue;
      px[yy * w + xx] = this->bg565_;
      px[yy * w + (w - 1 - xx)] = this->bg565_;
      px[(h - 1 - yy) * w + xx] = this->bg565_;
      px[(h - 1 - yy) * w + (w - 1 - xx)] = this->bg565_;
    }
  }
  span_sync(0, r, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  span_sync(h - r, r, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}

void MjpegStream::blit_() {
  if (this->disp_ == nullptr || this->ppa_ == nullptr || this->front_ == nullptr)
    return;
  const int64_t t0 = esp_timer_get_time();
#ifdef MJPEG_HAVE_DSI
  if (!this->fb_tried_) {
    this->fb_tried_ = true;
    auto *dsi = static_cast<mipi_dsi::MipiDsi *>(this->disp_);
    esp_lcd_panel_handle_t hdl = dsi->*dsi_get(DsiHandleTag());
    void *fb = nullptr;
    if (hdl != nullptr && esp_lcd_dpi_panel_get_frame_buffer(hdl, 1, &fb) == ESP_OK && fb != nullptr) {
      this->fb_ = (uint8_t *) fb;
      this->fb_w_ = this->disp_->get_native_width();
      this->fb_h_ = this->disp_->get_native_height();
      this->fb_size_ = (size_t) this->fb_w_ * this->fb_h_ * 2;
      ESP_LOGI(TAG, "Пряме виведення: PPA у кадровий буфер %dx%d", this->fb_w_, this->fb_h_);
    } else {
      ESP_LOGW(TAG, "Кадровий буфер недоступний — виведення через draw_pixels_at");
    }
  }
#endif
  this->round_corners_();
  if (this->fast_ && this->fb_ != nullptr) this->blit_fb_();
  else this->blit_copy_();
  this->have_frame_ = true;
  this->blit_us_ += (uint32_t) (esp_timer_get_time() - t0);
  this->blit_n_++;
}

// видима частина стовпця x відео — [top, bot) (виключення лише біля верхнього/нижнього краю)
#define MJPEG_SPAN(X, TOP, BOT)                                                \
  do {                                                                         \
    TOP = 0;                                                                   \
    BOT = h;                                                                   \
    for (auto &r : this->excl_) {                                              \
      if ((X) < r.x || (X) >= r.x + r.w || !this->excl_active_(r)) continue;   \
      if (r.y == 0) TOP = std::max(TOP, r.h);                                  \
      else BOT = std::min(BOT, r.y);                                           \
    }                                                                          \
  } while (0)

// Швидкий шлях: кожен блок стовпців однакової висоти повертається PPA одразу в кадровий буфер дисплея.
// Логічна точка відео (x, y) -> фізична (стовпець phys_x + h-1-y, рядок phys_y + x), як при LVGL rotation: 90.
void MjpegStream::blit_fb_() {
  const int w = this->width_, h = this->height_;
  const int phys_x = this->fb_w_ - this->dy_ - h;
  const int phys_y = this->dx_;
  if (phys_x < 0 || phys_y < 0 || phys_x + h > this->fb_w_ || phys_y + w > this->fb_h_) return;
  int x = 0;
  while (x < w) {
    int top, bot;
    MJPEG_SPAN(x, top, bot);
    int x2 = x + 1;
    while (x2 < w) {
      int t2, b2;
      MJPEG_SPAN(x2, t2, b2);
      if (t2 != top || b2 != bot) break;
      x2++;
    }
    const int cols = x2 - x, seg = bot - top;
    if (seg > 0) {
      ppa_srm_oper_config_t c = {};
      c.in.buffer = this->front_;
      c.in.pic_w = w;
      c.in.pic_h = h;
      c.in.block_w = cols;
      c.in.block_h = seg;
      c.in.block_offset_x = x;
      c.in.block_offset_y = top;
      c.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
      c.out.buffer = this->fb_;
      c.out.buffer_size = this->fb_size_;
      c.out.pic_w = this->fb_w_;
      c.out.pic_h = this->fb_h_;
      c.out.block_offset_x = phys_x + h - bot;
      c.out.block_offset_y = phys_y + x;
      c.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
      c.rotation_angle = PPA_SRM_ROTATION_ANGLE_270;
      c.scale_x = 1.0f;
      c.scale_y = 1.0f;
      // без очікування: PPA сам допише блок у кадровий буфер, основний цикл іде далі.
      // front_ не перезаписується, поки PPA його читає: наступний кадр декодується в інший буфер.
      c.mode = PPA_TRANS_MODE_NON_BLOCKING;
      esp_err_t e = ppa_do_scale_rotate_mirror(this->ppa_, &c);
      if (e == ESP_ERR_INVALID_STATE) {
        // черга повна (кадри приходять швидше, ніж PPA встигає) — цей блок пропускаємо
        continue_skip_++;
      } else if (e != ESP_OK) {
        ESP_LOGW(TAG, "PPA: помилка виведення в кадровий буфер (%s) — повертаюсь до копіювання", esp_err_to_name(e));
        this->fb_ = nullptr;
        return;
      }
    }
    x = x2;
  }
}

// Старий шлях: поворот у проміжний буфер, далі draw_pixels_at смугами
void MjpegStream::blit_copy_() {
  if (this->rot_ == nullptr) return;
  const int w = this->width_, h = this->height_;
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
  c.rotation_angle = PPA_SRM_ROTATION_ANGLE_270;
  c.scale_x = 1.0f;
  c.scale_y = 1.0f;
  c.mode = PPA_TRANS_MODE_BLOCKING;
  if (ppa_do_scale_rotate_mirror(this->ppa_, &c) != ESP_OK) {
    ESP_LOGW(TAG, "PPA: помилка повороту");
    return;
  }
  const int phys_x = this->disp_->get_native_width() - this->dy_ - h;
  const int phys_y = this->dx_;
  int x = 0;
  while (x < w) {
    int top, bot;
    MJPEG_SPAN(x, top, bot);
    int x2 = x + 1;
    while (x2 < w) {
      int t2, b2;
      MJPEG_SPAN(x2, t2, b2);
      if (t2 != top || b2 != bot) break;
      x2++;
    }
    const int cols = x2 - x, seg = bot - top;
    if (seg > 0) {
      const int r0 = h - bot;
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
      if (!this->excl_.empty() && this->excl_every_ > 0 && (this->frames_ % this->excl_every_) == 0 && lv_screen_active() != nullptr) {
        for (auto &r : this->excl_) {
          if (!this->excl_active_(r)) continue;
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
      ESP_LOGD(TAG, "%.1f кадр/с, виведення %.2f мс/кадр (%s)", this->fps_,
               this->blit_n_ ? this->blit_us_ / 1000.0f / this->blit_n_ : 0.0f,
               (this->fast_ && this->fb_ != nullptr) ? "PPA у кадровий буфер" : "копіювання");
      if (this->continue_skip_) ESP_LOGW(TAG, "PPA: пропущено блоків (черга повна): %u", (unsigned) this->continue_skip_);
      this->continue_skip_ = 0;
      this->blit_us_ = 0;
      this->blit_n_ = 0;
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
