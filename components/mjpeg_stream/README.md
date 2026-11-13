# mjpeg_stream — MJPEG-потік на екран ESP32-P4

Отримує MJPEG по HTTP (наприклад, go2rtc `api/stream.mjpeg`) і декодує кадри апаратним JPEG-декодером ESP32-P4 в окремій задачі. Кадр можна показати як зображення LVGL або вивести напряму в кадровий буфер.

Можливості:
- кілька потоків (`MULTI_CONF`); відкладений старт; дії `mjpeg_stream.start` / `mjpeg_stream.stop`; тригери `on_frame`, `on_connected`, `on_disconnected`;
- `direct_draw`: кадр повертається PPA на 270° (для `lvgl: rotation: 90`) і пишеться прямо в кадровий буфер, оминаючи LVGL; кути заокруглюються кольором фону;
- `exclude`: зони біля верхнього й нижнього краю кадру, куди відео напряму не пишеться. Там кадр малює LVGL (`bg_image`), тож напівпрозорі кнопки й написи поверх відео не перетираються.

```yaml
mjpeg_stream:
  - id: gate_stream
    url: "http://go2rtc:1984/api/stream.mjpeg?src=gate_panel"
    width: 656
    height: 384
    max_jpeg_size: 131072
    direct_draw:
      display: my_display
      x: 8
      y: 8
      radius: 14
      background: 0x0B1117
      exclude:
        - {x: 0, y: 0, width: 190, height: 44}
```
