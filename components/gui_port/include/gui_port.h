#pragma once

#include "esp_err.h"
#include "lvgl.h"
#include "gt911_input.h"
#include "display_orientation.h"

// Called from the single task that will also own every subsequent LVGL call.
// Initializes the board LCD, an LVGL RGB565 display and its PSRAM render buffer.
esp_err_t gui_port_init(lv_display_t **display);
typedef void (*gui_touch_observer_t)(const gt911_sample_t *sample, esp_err_t result);
// Must be called by the same task that creates the UI. Input is polled every 20 ms.
esp_err_t gui_port_add_touch(lv_display_t *display, gui_touch_observer_t observer,
                            lv_indev_t **indev);
// Runs the timer/render loop in the calling task. Returns only on display error.
esp_err_t gui_port_run(void (*cycle)(void));
// GUI owner task only; call between timer_handler/flush cycles.
esp_err_t gui_port_set_rotation(unsigned rotation);
bool gui_port_touch_blocked(void);
// Internal cooperation between renderer and input adapter, same GUI task only.
void gui_touch_rotation_changed(void);
void gui_touch_frame_presented(bool success);
