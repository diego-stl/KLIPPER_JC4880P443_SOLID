#include "board_sd.h"
#include "board_config.h"
#include "driver/sdmmc_host.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static sdmmc_card_t *s_card;
static esp_ldo_channel_handle_t s_power;
static portMUX_TYPE s_owner_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_owner;

static void release_owner(void)
{
    portENTER_CRITICAL(&s_owner_lock);
    s_owner = NULL;
    portEXIT_CRITICAL(&s_owner_lock);
}

static esp_err_t release_power(void)
{
    esp_err_t result = esp_ldo_release_channel(s_power);
    if (result == ESP_OK) s_power = NULL;
    return result;
}

esp_err_t board_sd_mount(void)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL(&s_owner_lock);
    bool available = s_owner == NULL;
    if (available) s_owner = task;
    portEXIT_CRITICAL(&s_owner_lock);
    if (!available) return ESP_ERR_INVALID_STATE;
    if (!s_power) {
        const esp_ldo_channel_config_t power = {
            .chan_id = BOARD_SD_LDO_CHANNEL,
            .voltage_mv = BOARD_SD_LDO_MV,
        };
        esp_err_t result = esp_ldo_acquire_channel(&power, &s_power);
        if (result != ESP_OK) {
            release_owner();
            return result;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.flags &= ~(SDMMC_HOST_FLAG_8BIT | SDMMC_HOST_FLAG_DDR);
    // Keep the default slot-scoped deinit. Slot 1 belongs to ESP-Hosted.
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    const esp_vfs_fat_sdmmc_mount_config_t filesystem = {
        .format_if_mount_failed = false,
        .max_files = 2,
    };
    sdmmc_card_t *card = NULL;
    esp_err_t result = esp_vfs_fat_sdmmc_mount(BOARD_SD_MOUNT_PATH, &host, &slot, &filesystem, &card);
    if (result == ESP_OK) {
        s_card = card;
    } else {
        if (release_power() != ESP_OK) ESP_LOGW("board_sd", "No se pudo liberar VO4");
        release_owner();
    }
    return result;
}

esp_err_t board_sd_unmount(void)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL(&s_owner_lock);
    bool owned = s_owner == task;
    portEXIT_CRITICAL(&s_owner_lock);
    if (!owned || !s_card) return ESP_ERR_INVALID_STATE;
    esp_err_t result = esp_vfs_fat_sdcard_unmount(BOARD_SD_MOUNT_PATH, s_card);
    s_card = NULL;
    esp_err_t power_result = release_power();
    release_owner();
    return result == ESP_OK ? power_result : result;
}
