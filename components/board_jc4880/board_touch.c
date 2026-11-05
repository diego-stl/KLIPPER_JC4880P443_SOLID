#include "board_touch.h"
#include "board_config.h"

#include <string.h>
#include "esp_check.h"

static const char *TAG = "board_touch";
static i2c_master_bus_handle_t s_bus;

static esp_err_t read_register(i2c_master_dev_handle_t device, uint16_t reg,
                               uint8_t *data, size_t bytes)
{
    const uint8_t pointer[] = {(uint8_t)(reg >> 8), (uint8_t)reg};
    return i2c_master_transmit_receive(device, pointer, sizeof(pointer), data, bytes, 100);
}

i2c_master_bus_handle_t board_i2c_bus(void)
{
    return s_bus;
}

esp_err_t board_touch_probe(board_touch_info_t *info)
{
    ESP_RETURN_ON_FALSE(info != NULL, ESP_ERR_INVALID_ARG, TAG, "Info nula");
    memset(info, 0, sizeof(*info));
    if (!s_bus) {
        const i2c_master_bus_config_t bus_config = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = BOARD_TOUCH_SDA_GPIO,
            .scl_io_num = BOARD_TOUCH_SCL_GPIO,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &s_bus), TAG, "Crear I2C");
    }
    ESP_LOGI(TAG, "Sondeo GT911: SDA=%d SCL=%d; reset/INT sin configurar",
             BOARD_TOUCH_SDA_GPIO, BOARD_TOUCH_SCL_GPIO);
    const uint8_t candidates[] = {0x5d, 0x14};
    for (size_t i = 0; i < sizeof(candidates); ++i) {
        esp_err_t result = i2c_master_probe(s_bus, candidates[i], 100);
        ESP_LOGI(TAG, "Direccion 0x%02x: %s", candidates[i], esp_err_to_name(result));
        if (result == ESP_OK) {
            info->address = candidates[i];
            break;
        }
        if (result != ESP_ERR_NOT_FOUND) {
            return result;
        }
    }
    ESP_RETURN_ON_FALSE(info->address != 0, ESP_ERR_NOT_FOUND, TAG, "GT911 no responde");
    i2c_master_dev_handle_t device = NULL;
    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = info->address,
        .scl_speed_hz = BOARD_TOUCH_I2C_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &device_config, &device), TAG, "Crear dispositivo");
    uint8_t identity[6] = {0};
    uint8_t config[5] = {0};
    esp_err_t result = read_register(device, 0x8140, identity, sizeof(identity));
    if (result == ESP_OK) {
        result = read_register(device, 0x8047, config, sizeof(config));
    }
    i2c_master_bus_rm_device(device);
    ESP_RETURN_ON_ERROR(result, TAG, "Leer identidad/configuracion");
    memcpy(info->product_id, identity, 4);
    info->firmware = identity[4] | ((uint16_t)identity[5] << 8);
    info->config_version = config[0];
    info->x_resolution = config[1] | ((uint16_t)config[2] << 8);
    info->y_resolution = config[3] | ((uint16_t)config[4] << 8);
    ESP_LOGI(TAG, "PID='%.4s' (%02x %02x %02x %02x), firmware=0x%04x, config=%u",
             info->product_id, identity[0], identity[1], identity[2], identity[3],
             info->firmware, info->config_version);
    ESP_LOGI(TAG, "Resolucion configurada GT911: %u x %u; I2C %d Hz; solo lectura",
             info->x_resolution, info->y_resolution, BOARD_TOUCH_I2C_HZ);
    ESP_RETURN_ON_FALSE(memcmp(identity, "911", 3) == 0, ESP_ERR_INVALID_RESPONSE, TAG, "PID distinto de GT911");
    return ESP_OK;
}
