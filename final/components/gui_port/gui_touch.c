#include "gui_port.h"
#include "gui_metrics.h"
#include "esp_check.h"
#include <inttypes.h>
#include "esp_timer.h"

static const char *TAG = "gui_touch";
static gui_touch_observer_t s_observer;
static gt911_sample_t s_last;
static unsigned s_errors;
static int64_t s_max_read_us;
static int64_t s_last_report;
static lv_indev_t *s_indev;
static orientation_gate_t s_gate;
static bool s_disabled;

bool gui_port_touch_blocked(void) { return s_gate.blocked || s_disabled; }
void gui_touch_rotation_changed(void)
{
    orientation_gate_begin(&s_gate);
    if (s_indev && !s_disabled) {
        lv_indev_reset(s_indev, NULL);
        lv_indev_wait_release(s_indev);
    }
}
void gui_touch_frame_presented(bool success) { orientation_gate_frame(&s_gate, success); }

static void read_touch(lv_indev_t *indev, lv_indev_data_t *data)
{
    gt911_sample_t sample = s_last;
    int64_t start = esp_timer_get_time();
    esp_err_t result = gt911_input_read(&sample);
    int64_t elapsed = esp_timer_get_time() - start;
    gui_metrics_touch(start,(uint32_t)elapsed);
    if (elapsed > s_max_read_us) s_max_read_us = elapsed;
    if (esp_timer_get_time() - s_last_report > 10000000) {
        ESP_LOGI(TAG, "Tiempo maximo lectura GT911=%" PRId64 " us", s_max_read_us);
        s_max_read_us = 0;
        s_last_report = esp_timer_get_time();
    }
    if (result == ESP_OK) {
        s_errors = 0;
        s_last = sample;
        bool blocked = s_gate.blocked;
        orientation_gate_sample(&s_gate, true, sample.fresh, sample.pressed);
        if (blocked && !s_gate.blocked) {
            ESP_LOGI(TAG, "TOUCH_ARMED rot=%u; cuadro presentado y liberacion fresca",
                     (unsigned)orientation_current());
        }
    } else if (++s_errors >= 3) {
        // Cancel the current gesture before disabling. Never turn bus errors into clicks.
        lv_indev_reset(indev, NULL);
        lv_indev_enable(indev, false);
        s_disabled = true;
        s_last.pressed = false;
        ESP_LOGE(TAG, "Entrada deshabilitada tras 3 errores; revisar registro y reiniciar");
    }
    data->point.x = s_last.x;
    data->point.y = s_last.y;
    data->state = s_last.pressed && !gui_port_touch_blocked()
        ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    if (s_observer) {
        s_observer(&sample, result);
    }
}

esp_err_t gui_port_add_touch(lv_display_t *display, gui_touch_observer_t observer,
                            lv_indev_t **indev)
{
    ESP_RETURN_ON_FALSE(display != NULL && indev != NULL, ESP_ERR_INVALID_ARG, TAG, "Argumentos nulos");
    s_disabled = true;
    ESP_RETURN_ON_ERROR(gt911_input_init(), TAG, "Inicializar GT911");
    *indev = lv_indev_create();
    ESP_RETURN_ON_FALSE(*indev != NULL, ESP_ERR_NO_MEM, TAG, "Crear entrada LVGL");
    s_observer = observer;
    lv_indev_set_type(*indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(*indev, display);
    lv_indev_set_read_cb(*indev, read_touch);
    s_indev = *indev;
    s_disabled = false;
    lv_timer_set_period(lv_indev_get_read_timer(*indev), 20);
    ESP_LOGI(TAG, "Entrada LVGL activa; pulsaciones, liberacion y mapa por validar con el usuario");
    return ESP_OK;
}
