#include "sdkconfig.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "orientation_test.h"
#include "gui_port.h"
#if !CONFIG_PANEL_ORIENTATION_TEST
#include "portal_ui.h"
#include "device_network.h"
#include "klipper_client.h"
#include "panel_console.h"
#include "audio_service.h"
#include "audio_events.h"
#endif
#if !CONFIG_ESP32P4_SELECTS_REV_LESS_V3 || CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ != 360
#error "Conservar P4 pre-v3 a 360 MHz"
#endif
#if CONFIG_PANEL_ORIENTATION_TEST && (CONFIG_ESP_HOSTED_ENABLED || CONFIG_ESP_WIFI_REMOTE_ENABLED)
#error "Esta prueba no debe iniciar el coprocesador/Wi-Fi"
#endif
#if !CONFIG_PANEL_ORIENTATION_TEST
static bool application_console(const char *command)
{
    return gui_port_console_command(command) || portal_ui_console_command(command) || audio_events_console_command(command) || audio_service_console_command(command) || panel_console_command(command);
}

#ifdef CONFIG_PORTAL_CORE_HD
static void application_console_poll(void)
{
    portal_ui_start_standby_clip_load();
    gui_port_console_poll();
}
#endif
#endif
void app_main(void)
{
    esp_chip_info_t chip;
    uint32_t flash_bytes = 0;
    esp_chip_info(&chip);
    ESP_ERROR_CHECK(esp_flash_get_size(NULL, &flash_bytes));
    ESP_LOGI("panel", "IDF=%s P4=v%d.%d CPU=%d; montaje R90 aceptado",
             esp_get_idf_version(), chip.revision / 100, chip.revision % 100,
             CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    if (chip.revision >= 300 || chip.cores != 2 || flash_bytes != 16U*1024U*1024U ||
        esp_psram_get_size() != 32U*1024U*1024U) {
        ESP_LOGE("orientation", "Hardware diferente del perfil validado; no iniciar LCD");
        return;
    }
#if CONFIG_PANEL_ORIENTATION_TEST
    orientation_test_run();
#else
    lv_display_t *display = NULL;
    lv_indev_t *input = NULL;
    ESP_ERROR_CHECK(gui_port_init(&display));
    ESP_ERROR_CHECK(gui_port_add_touch(display, NULL, &input));
    portal_ui_init(display, input);
    device_network_set_console_handler(application_console);
#ifdef CONFIG_PORTAL_CORE_HD
    device_network_set_console_poll_handler(application_console_poll);
#else
    device_network_set_console_poll_handler(gui_port_console_poll);
#endif
    ESP_ERROR_CHECK(device_network_start());
    ESP_ERROR_CHECK(klipper_client_start());
    esp_err_t audio = audio_service_start();
    if (audio != ESP_OK) ESP_LOGE("panel", "Audio no disponible: %s; PORTAL continua", esp_err_to_name(audio));
    ESP_LOGI("panel", "PORTAL_READY UI=800x480 R90; sonidos de Portal; red y Moonraker por consola");
    gui_port_run(portal_ui_tick);
#endif
}
