#pragma once

#include "esp_err.h"

#define BOARD_SD_MOUNT_PATH "/sdcard"

// A successful mount reserves the card until the same task unmounts it.
// Close all files before unmounting. A simultaneous mount returns INVALID_STATE.
esp_err_t board_sd_mount(void);
esp_err_t board_sd_unmount(void);
