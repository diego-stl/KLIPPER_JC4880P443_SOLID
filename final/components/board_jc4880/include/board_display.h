#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// Single rendering task. Initialization leaves backlight OFF and fb0 scanning black.
esp_err_t board_display_init(void);
// Full 480x800 RGB565 buffer; valid until present(). Never write it after present().
uint16_t *board_display_back_buffer(void);
// Full-frame zero-copy presentation; blocks until the previous buffer is reusable.
// On a DMA timeout, backlight is turned off and rendering must stop.
esp_err_t board_display_present(void);
esp_err_t board_display_set_backlight(bool enabled);
// PWM brightness, 0..100. Keeps the on/off state used by display fault handling.
esp_err_t board_display_set_brightness(uint8_t percent);
