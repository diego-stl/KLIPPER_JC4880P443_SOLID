#pragma once
#include <stdint.h>
typedef enum { GUI_METRIC_UI,GUI_METRIC_LVGL,GUI_METRIC_RENDER,GUI_METRIC_ROTATE,
               GUI_METRIC_PRESENT,GUI_METRIC_TOUCH,GUI_METRIC_COUNT } gui_metric_t;
// Called only by the GUI owner. No allocations, task scans, or LVGL calls.
void gui_metrics_record(gui_metric_t metric,uint32_t elapsed_us);
void gui_metrics_frame(uint32_t rotated_pixels);
void gui_metrics_touch(int64_t read_started_us,uint32_t elapsed_us);
void gui_metrics_poll(int64_t now_us);
