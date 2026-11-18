#pragma once

#include "driver/gpio.h"

// Source revisions and unresolved signals are documented in docs/hardware-reference.md.
#define BOARD_LCD_RESET_GPIO       GPIO_NUM_5
#define BOARD_LCD_BACKLIGHT_GPIO   GPIO_NUM_23
#define BOARD_DSI_LDO_CHANNEL      3
#define BOARD_DSI_LDO_MV           2500
#define BOARD_DSI_LANES            2
#define BOARD_DSI_LANE_MBPS        500
#define BOARD_LCD_WIDTH            480
#define BOARD_LCD_HEIGHT           800
#define BOARD_LCD_PCLK_MHZ         34
#define BOARD_LCD_HSYNC_PULSE       12
#define BOARD_LCD_HBP               42
#define BOARD_LCD_HFP               42
#define BOARD_LCD_VSYNC_PULSE       2
#define BOARD_LCD_VBP               8
#define BOARD_LCD_VFP               166
#define BOARD_LCD_FRAMEBUFFERS      2

// Native panel remains 480x800. UI rotation is owned by display_orientation.

// Reserved for stage C. Do not configure reset/INT until verified on this unit.
#define BOARD_TOUCH_SDA_GPIO        GPIO_NUM_7
#define BOARD_TOUCH_SCL_GPIO        GPIO_NUM_8
#define BOARD_TOUCH_I2C_HZ          100000
#define BOARD_TOUCH_RESET_GPIO      GPIO_NUM_NC
#define BOARD_TOUCH_INT_GPIO        GPIO_NUM_NC

// TF_VCC is powered by VO4. The microSD pins are the P4 slot 0 IOMUX pins.
#define BOARD_SD_LDO_CHANNEL        4
#define BOARD_SD_LDO_MV             3300
