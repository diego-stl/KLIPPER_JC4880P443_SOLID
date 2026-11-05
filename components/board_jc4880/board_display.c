#include "board_config.h"
#include "board_display.h"
#include "st7701_sequence.h"

#include "esp_attr.h"
#include "esp_check.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/ledc.h"

static const char *TAG = "board_display";
static esp_ldo_channel_handle_t s_phy_ldo;
static esp_lcd_dsi_bus_handle_t s_dsi;
static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_fb[BOARD_LCD_FRAMEBUFFERS];
static unsigned s_back_index = 1;
static bool s_ready;
static StaticSemaphore_t s_frame_sem_storage;
static SemaphoreHandle_t s_frame_sem;
static bool s_pwm_ready,s_backlight_enabled;
static uint8_t s_brightness=100;

static bool IRAM_ATTR frame_complete(esp_lcd_panel_handle_t panel,
                                    esp_lcd_dpi_panel_event_data_t *event,
                                    void *user_ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)user_ctx, &woken);
    return woken == pdTRUE;
}

esp_err_t board_display_init(void)
{
    esp_err_t ret;
    ESP_RETURN_ON_FALSE(!s_ready && !s_panel, ESP_ERR_INVALID_STATE, TAG, "Ya inicializado");
    const gpio_config_t outputs = {
        .pin_bit_mask = (1ULL << BOARD_LCD_RESET_GPIO) | (1ULL << BOARD_LCD_BACKLIGHT_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    // Preload low output levels before enabling the GPIO output drivers.
    ESP_RETURN_ON_ERROR(gpio_set_level(BOARD_LCD_BACKLIGHT_GPIO, 0), TAG, "Backlight off");
    ESP_RETURN_ON_ERROR(gpio_set_level(BOARD_LCD_RESET_GPIO, 0), TAG, "Reset low");
    ESP_RETURN_ON_ERROR(gpio_config(&outputs), TAG, "GPIO LCD");

    const esp_ldo_channel_config_t ldo = {
        .chan_id = BOARD_DSI_LDO_CHANNEL,
        .voltage_mv = BOARD_DSI_LDO_MV,
    };
    ESP_GOTO_ON_ERROR(esp_ldo_acquire_channel(&ldo, &s_phy_ldo), fail, TAG, "LDO PHY");
    ESP_LOGI(TAG, "DSI PHY: LDO%d = %d mV", BOARD_DSI_LDO_CHANNEL, BOARD_DSI_LDO_MV);

    const esp_lcd_dsi_bus_config_t dsi = {
        .bus_id = 0,
        .num_data_lanes = BOARD_DSI_LANES,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = BOARD_DSI_LANE_MBPS,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_dsi_bus(&dsi, &s_dsi), fail, TAG, "Bus DSI");
    const esp_lcd_dbi_io_config_t io = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_dbi(s_dsi, &io, &s_io), fail, TAG, "IO DBI");
    const esp_lcd_dpi_panel_config_t dpi = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = BOARD_LCD_PCLK_MHZ,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = BOARD_LCD_FRAMEBUFFERS,
        .video_timing = {
            .h_size = BOARD_LCD_WIDTH,
            .v_size = BOARD_LCD_HEIGHT,
            .hsync_pulse_width = BOARD_LCD_HSYNC_PULSE,
            .hsync_back_porch = BOARD_LCD_HBP,
            .hsync_front_porch = BOARD_LCD_HFP,
            .vsync_pulse_width = BOARD_LCD_VSYNC_PULSE,
            .vsync_back_porch = BOARD_LCD_VBP,
            .vsync_front_porch = BOARD_LCD_VFP,
        },
        // We render directly into the driver's back buffer; DMA2D copy is unnecessary.
        .flags.use_dma2d = false,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_dpi(s_dsi, &dpi, &s_panel), fail, TAG, "Panel DPI");
    void *fb0 = NULL;
    void *fb1 = NULL;
    ESP_GOTO_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(s_panel, 2, &fb0, &fb1),
                      fail, TAG, "Framebuffers");
    ESP_GOTO_ON_FALSE(esp_ptr_external_ram(fb0) && esp_ptr_external_ram(fb1),
                      ESP_ERR_INVALID_STATE, fail, TAG, "Framebuffers fuera de PSRAM");
    s_fb[0] = fb0;
    s_fb[1] = fb1;
    s_back_index = 1;
    // IDF allocates, clears and writes back both framebuffers before starting DMA.
    s_frame_sem = xSemaphoreCreateBinaryStatic(&s_frame_sem_storage);
    const esp_lcd_dpi_panel_event_callbacks_t callbacks = {
        .on_frame_buf_complete = frame_complete,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_dpi_panel_register_event_callbacks(s_panel, &callbacks, s_frame_sem),
                      fail, TAG, "Callback DMA");

    ESP_GOTO_ON_ERROR(gpio_set_level(BOARD_LCD_RESET_GPIO, 0), fail, TAG, "Reset low");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_GOTO_ON_ERROR(gpio_set_level(BOARD_LCD_RESET_GPIO, 1), fail, TAG, "Reset high");
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_GOTO_ON_ERROR(st7701_sequence_send(s_io), fail, TAG, "Tabla ST7701S");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_init(s_panel), fail, TAG, "Inicio DPI");
    s_ready = true;
    ESP_LOGI(TAG, "%dx%d RGB565; DSI %d x %d Mbps; PCLK %d MHz",
             BOARD_LCD_WIDTH, BOARD_LCD_HEIGHT, BOARD_DSI_LANES,
             BOARD_DSI_LANE_MBPS, BOARD_LCD_PCLK_MHZ);
    ESP_LOGI(TAG, "PSRAM fb0=%p fb1=%p, %zu bytes cada uno; backlight apagado",
             fb0, fb1, (size_t)BOARD_LCD_WIDTH * BOARD_LCD_HEIGHT * sizeof(uint16_t));
    return ESP_OK;

fail:
    gpio_set_level(BOARD_LCD_BACKLIGHT_GPIO, 0);
    if (s_panel) {
        esp_lcd_panel_del(s_panel);
        s_panel = NULL;
    }
    if (s_io) {
        esp_lcd_panel_io_del(s_io);
        s_io = NULL;
    }
    if (s_dsi) {
        esp_lcd_del_dsi_bus(s_dsi);
        s_dsi = NULL;
    }
    if (s_phy_ldo) {
        esp_ldo_release_channel(s_phy_ldo);
        s_phy_ldo = NULL;
    }
    s_fb[0] = s_fb[1] = NULL;
    return ret;
}

uint16_t *board_display_back_buffer(void)
{
    return s_ready ? s_fb[s_back_index] : NULL;
}

esp_err_t board_display_present(void)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "Pantalla no inicializada");
    // Drop any old completion. An ISR can still race with the request below.
    xSemaphoreTake(s_frame_sem, 0);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(s_panel, 0, 0, BOARD_LCD_WIDTH,
                                                BOARD_LCD_HEIGHT, s_fb[s_back_index]),
                        TAG, "Presentar framebuffer");
    // IDF 5.5.6 chooses the next FB in the DMA completion ISR. Two completions
    // cover an already-running ISR that sampled the old FB before draw_bitmap.
    // This is NOT on_color_trans_done: that callback can run before scanout.
    for (unsigned i = 0; i < 2; ++i) {
        if (xSemaphoreTake(s_frame_sem, pdMS_TO_TICKS(500)) != pdTRUE) {
            board_display_set_backlight(false);
            ESP_LOGE(TAG, "Timeout de cuadro DMA; backlight apagado; detener renderizado");
            return ESP_ERR_TIMEOUT;
        }
    }
    s_back_index ^= 1;
    return ESP_OK;
}

esp_err_t board_display_set_backlight(bool enabled)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "Pantalla no inicializada");
    s_backlight_enabled=enabled;
    if(s_pwm_ready){
        ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,
                            enabled?(1023U*s_brightness/100U):0),TAG,"Brillo LEDC");
        return ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0);
    }
    return gpio_set_level(BOARD_LCD_BACKLIGHT_GPIO, enabled ? 1 : 0);
}

esp_err_t board_display_set_brightness(uint8_t percent)
{
    ESP_RETURN_ON_FALSE(s_ready&&percent<=100,ESP_ERR_INVALID_ARG,TAG,"Brillo invalido");
    if(!s_pwm_ready){
        // MP3202 dimming through EN: the datasheet specifies PWM at 1 kHz or below.
        ledc_timer_config_t timer={.speed_mode=LEDC_LOW_SPEED_MODE,.duty_resolution=LEDC_TIMER_10_BIT,
            .timer_num=LEDC_TIMER_0,.freq_hz=100,.clk_cfg=LEDC_AUTO_CLK};
        ESP_RETURN_ON_ERROR(ledc_timer_config(&timer),TAG,"Timer brillo");
        ledc_channel_config_t channel={.gpio_num=BOARD_LCD_BACKLIGHT_GPIO,.speed_mode=LEDC_LOW_SPEED_MODE,
            .channel=LEDC_CHANNEL_0,.timer_sel=LEDC_TIMER_0,.duty=s_backlight_enabled?1023U*percent/100U:0};
        ESP_RETURN_ON_ERROR(ledc_channel_config(&channel),TAG,"Canal brillo");
        s_pwm_ready=true;
        ESP_LOGI(TAG,"Backlight PWM: GPIO%d, %u Hz, 10 bits",BOARD_LCD_BACKLIGHT_GPIO,
                 (unsigned)ledc_get_freq(LEDC_LOW_SPEED_MODE,LEDC_TIMER_0));
    }
    s_brightness=percent;
    return board_display_set_backlight(s_backlight_enabled);
}
