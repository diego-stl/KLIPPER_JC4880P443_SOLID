#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"

// Panel must already be powered and hardware-reset by the board layer.
// Includes SLPOUT/DISPON and their delays. Board-specific table, not generic ST7701.
esp_err_t st7701_sequence_send(esp_lcd_panel_io_handle_t io);
