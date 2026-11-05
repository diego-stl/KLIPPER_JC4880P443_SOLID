#include "gui_port.h"
#include "board_config.h"
#include "board_display.h"

#include <inttypes.h>
#include <string.h>
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "gui_port";
_Static_assert(BOARD_LCD_WIDTH == ORIENTATION_PANEL_W && BOARD_LCD_HEIGHT == ORIENTATION_PANEL_H,
               "Conservar el panel nativo 480x800");
static esp_err_t s_error;
static uint32_t s_frames;
static int64_t s_max_rotate_us;
static int64_t s_max_present_us;
static int64_t s_render_start;
static int64_t s_max_render_us;
static lv_display_t *s_display;
static bool s_rotation_frame = true;

static void render_timing(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_RENDER_START) {
        s_render_start = esp_timer_get_time();
    } else if (lv_event_get_code(event) == LV_EVENT_RENDER_READY) {
        int64_t elapsed = esp_timer_get_time() - s_render_start;
        if (elapsed > s_max_render_us) s_max_render_us = elapsed;
    }
}

static uint32_t tick_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    if (s_error != ESP_OK) {
        lv_display_flush_ready(display);
        return;
    }
    if (area->x1 != 0 || area->y1 != 0 ||
        area->x2 != ORIENTATION_UI_W - 1 || area->y2 != ORIENTATION_UI_H - 1) {
        ESP_LOGE(TAG, "El puerto requiere un cuadro completo RGB565");
        s_error = ESP_ERR_INVALID_SIZE;
    } else {
        uint16_t *back = board_display_back_buffer();
        const uint16_t *source = (const uint16_t *)pixels;
        int64_t start = esp_timer_get_time();
        if (!orientation_rotate_rgb565(orientation_current(), source, back)) {
            s_error = ESP_ERR_INVALID_STATE;
            board_display_set_backlight(false);
            lv_display_flush_ready(display);
            return;
        }
        int64_t rotate_us = esp_timer_get_time() - start;
        if (rotate_us > s_max_rotate_us) s_max_rotate_us = rotate_us;
        start = esp_timer_get_time();
        s_error = board_display_present();
        gui_touch_frame_presented(s_error == ESP_OK);
        if (s_error == ESP_OK && s_rotation_frame) {
            ESP_LOGI(TAG, "ORIENTATION_FRAME rot=%u; UI=800x480 panel=480x800",
                     (unsigned)orientation_current());
            s_rotation_frame = false;
        }
        int64_t present_us = esp_timer_get_time() - start;
        if (present_us > s_max_present_us) s_max_present_us = present_us;
        if (s_error == ESP_OK && s_frames++ == 0) {
            s_error = board_display_set_backlight(true);
            ESP_LOGI(TAG, "Primer cuadro LVGL entregado; lectura y orientacion por validar");
        }
    }
    if (s_error != ESP_OK) {
        board_display_set_backlight(false);
    }
    // One full LVGL buffer: no rendering overlaps the rotation or scanout handoff.
    lv_display_flush_ready(display);
}

esp_err_t gui_port_init(lv_display_t **display)
{
    ESP_RETURN_ON_FALSE(display != NULL, ESP_ERR_INVALID_ARG, TAG, "Display nulo");
    ESP_RETURN_ON_ERROR(board_display_init(), TAG, "Inicializar pantalla");
    lv_init();
    lv_tick_set_cb(tick_ms);
    const size_t buffer_bytes = (size_t)BOARD_LCD_WIDTH * BOARD_LCD_HEIGHT * sizeof(uint16_t);
    void *render_buffer = heap_caps_aligned_alloc(64, buffer_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(render_buffer != NULL, ESP_ERR_NO_MEM, TAG, "Buffer LVGL PSRAM");
    *display = lv_display_create(ORIENTATION_UI_W, ORIENTATION_UI_H);
    if (*display == NULL) {
        heap_caps_free(render_buffer);
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_color_format(*display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(*display, render_buffer, NULL, buffer_bytes, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(*display, flush);
    lv_display_add_event_cb(*display, render_timing, LV_EVENT_RENDER_START, NULL);
    lv_display_add_event_cb(*display, render_timing, LV_EVENT_RENDER_READY, NULL);
    s_display = *display;
    ESP_LOGI(TAG, "LVGL %d.%d.%d; render PSRAM %zu bytes; giro de salida %d grados",
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
             buffer_bytes, (int)orientation_current());
    return ESP_OK;
}

esp_err_t gui_port_set_rotation(unsigned rotation)
{
    ESP_RETURN_ON_FALSE(s_display && orientation_valid(rotation), ESP_ERR_INVALID_ARG,
                        TAG, "Giro debe ser 90 o 270");
    if (orientation_current() == rotation) return ESP_OK;
    gui_touch_rotation_changed();
    orientation_select(rotation);
    s_rotation_frame = true;
    lv_obj_invalidate(lv_display_get_screen_active(s_display));
    ESP_LOGI(TAG, "ORIENTATION_CHANGED rot=%u; esperando cuadro y liberacion fresca", rotation);
    return ESP_OK;
}

esp_err_t gui_port_run(void (*cycle)(void))
{
    int64_t last_report = esp_timer_get_time();
    int64_t max_handler_us = 0;
    while (s_error == ESP_OK) {
        if (cycle) cycle();
        int64_t handler_start = esp_timer_get_time();
        uint32_t delay_ms = lv_timer_handler();
        int64_t handler_us = esp_timer_get_time() - handler_start;
        if (handler_us > max_handler_us) max_handler_us = handler_us;
        if (delay_ms > 20) {
            delay_ms = 20;
        }
        if (delay_ms < 10) {
            delay_ms = 10;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        if (esp_timer_get_time() - last_report >= 10000000) {
            ESP_LOGI(TAG, "Activo: cuadros=%" PRIu32 "; PSRAM libre=%zu; stack libre=%u bytes",
                     s_frames, heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                     (unsigned)uxTaskGetStackHighWaterMark(NULL));
            last_report = esp_timer_get_time();
            ESP_LOGI(TAG, "Tiempo maximo us: LVGL=%" PRId64 "; render=%" PRId64 "; giro=%" PRId64 "; presentar=%" PRId64,
                     max_handler_us, s_max_render_us, s_max_rotate_us, s_max_present_us);
            max_handler_us = s_max_render_us = s_max_rotate_us = s_max_present_us = 0;
        }
    }
    ESP_LOGE(TAG, "Render detenido: %s", esp_err_to_name(s_error));
    return s_error;
}
