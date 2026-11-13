#include "gt911_input.h"
#include "esp_lcd_touch_gt911.h"
#include "board_config.h"
#include "board_touch.h"
#include "esp_check.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_io.h"
#include "display_orientation.h"

static const char *TAG = "gt911_input";
static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_touch_handle_t s_touch;
static esp_lcd_touch_io_gt911_config_t s_address_config;
static gt911_sample_t s_sample;

esp_err_t gt911_input_init(void)
{
    ESP_RETURN_ON_FALSE(s_touch == NULL, ESP_ERR_INVALID_STATE, TAG, "Ya inicializado");
    board_touch_info_t info;
    ESP_RETURN_ON_ERROR(board_touch_probe(&info), TAG, "Sondeo GT911");
    ESP_RETURN_ON_FALSE(info.x_resolution == BOARD_LCD_WIDTH && info.y_resolution == BOARD_LCD_HEIGHT,
                        ESP_ERR_NOT_SUPPORTED, TAG, "Resolucion tactil distinta del perfil comprobado");
    esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_config.dev_addr = info.address;
    io_config.scl_speed_hz = BOARD_TOUCH_I2C_HZ;
    io_config.transaction_timeout_ms = 50;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(board_i2c_bus(), &io_config, &s_io), TAG, "IO tactil");
    s_address_config.dev_addr = info.address;
    const esp_lcd_touch_config_t config = {
        .x_max = BOARD_LCD_WIDTH,
        .y_max = BOARD_LCD_HEIGHT,
        .rst_gpio_num = BOARD_TOUCH_RESET_GPIO,
        .int_gpio_num = BOARD_TOUCH_INT_GPIO,
        .driver_data = &s_address_config,
        // Raw native coordinates: rotation is applied exactly once below.
        .flags = {.swap_xy = false, .mirror_x = false, .mirror_y = false},
    };
    esp_err_t result = esp_lcd_touch_new_i2c_gt911(s_io, &config, &s_touch);
    if (result != ESP_OK) {
        esp_lcd_panel_io_del(s_io);
        s_io = NULL;
        return result;
    }
    ESP_LOGI(TAG, "Driver GT911 1.2.1; raw nativo; sin reset/INT; UI R%u",
             (unsigned)orientation_current());
    return ESP_OK;
}

esp_err_t gt911_input_read(gt911_sample_t *sample)
{
    ESP_RETURN_ON_FALSE(sample != NULL && s_touch != NULL, ESP_ERR_INVALID_STATE, TAG, "Entrada no inicializada");
    *sample = s_sample;
    sample->fresh = false;
    // Re-map retained raw coordinates too, including no-report/release after a turn.
    orientation_map_touch(orientation_current(), sample->raw_x, sample->raw_y,
                          &sample->x, &sample->y);
    uint8_t status = 0;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_rx_param(s_io, 0x814e, &status, 1), TAG, "Leer estado");
    // No report does NOT mean released. Do not let the driver clear an unread report.
    if ((status & 0x80) == 0) {
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE((status & 0x0f) <= 5, ESP_ERR_INVALID_RESPONSE, TAG, "Conteo tactil invalido");
    ESP_RETURN_ON_ERROR(esp_lcd_touch_read_data(s_touch), TAG, "Leer muestra");
    esp_lcd_touch_point_data_t point;
    uint8_t count = 0;
    ESP_RETURN_ON_ERROR(esp_lcd_touch_get_data(s_touch, &point, &count, 1), TAG, "Obtener punto");
    if (count > 0) {
        ESP_RETURN_ON_FALSE(point.x < BOARD_LCD_WIDTH && point.y < BOARD_LCD_HEIGHT,
                            ESP_ERR_INVALID_RESPONSE, TAG, "Punto fuera de rango: %u,%u", point.x, point.y);
        s_sample.raw_x = point.x;
        s_sample.raw_y = point.y;
    }
    ESP_RETURN_ON_FALSE(orientation_map_touch(orientation_current(), s_sample.raw_x,
                        s_sample.raw_y, &s_sample.x, &s_sample.y),
                        ESP_ERR_INVALID_STATE, TAG, "Transformacion tactil invalida");
    s_sample.pressed = count > 0;
    s_sample.fresh = true;
    *sample = s_sample;
    return ESP_OK;
}
