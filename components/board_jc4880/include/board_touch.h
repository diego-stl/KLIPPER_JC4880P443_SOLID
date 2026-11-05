#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

typedef struct {
    uint8_t address;
    char product_id[5];
    uint16_t firmware;
    uint8_t config_version;
    uint16_t x_resolution;
    uint16_t y_resolution;
} board_touch_info_t;

// Owns the board's shared I2C bus. Does not drive touch reset/INT or write config.
esp_err_t board_touch_probe(board_touch_info_t *info);
i2c_master_bus_handle_t board_i2c_bus(void);
