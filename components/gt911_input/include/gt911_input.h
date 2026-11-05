#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool pressed;
    bool fresh;
    uint16_t raw_x;
    uint16_t raw_y;
    uint16_t x;
    uint16_t y;
} gt911_sample_t;

// Uses the existing board I2C bus; reset and INT remain unconfigured.
esp_err_t gt911_input_init(void);
// Poll from one task. If no new valid report exists, retains the previous state.
esp_err_t gt911_input_read(gt911_sample_t *sample);
