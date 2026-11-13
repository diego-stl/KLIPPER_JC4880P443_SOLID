#pragma once
#include <stdbool.h>
#include "lvgl.h"
// All four calls belong to the LVGL owner task. The guard is checked by every
// callback and tick; it must include inspection, sleeping and pending inspection.
// Moonraker input is IPv4-only; DNS endpoints remain supported by the console.
void portal_network_settings_open(lv_obj_t *parent, bool (*input_blocked)(void));
void portal_network_settings_tick(void);
void portal_network_settings_close(void);
bool portal_network_settings_is_open(void);
