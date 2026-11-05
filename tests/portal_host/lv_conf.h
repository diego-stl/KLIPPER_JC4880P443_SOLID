#ifndef LV_CONF_H
#define LV_CONF_H
#define LV_COLOR_DEPTH 16
#define LV_USE_OS LV_OS_NONE
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB
#include <stdio.h>
#include <stdlib.h>
#define LV_USE_LOG 1
#define LV_LOG_PRINTF 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_ASSERT_HANDLER fprintf(stderr,"LVGL ASSERT %s:%d\n",__FILE__,__LINE__); abort();
#define LV_DRAW_BUF_STRIDE_ALIGN 1
#define LV_DRAW_BUF_ALIGN 4
#define LV_USE_DRAW_SW 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14
#define LV_USE_LODEPNG 1
#define LV_USE_FLOAT 1
#endif
