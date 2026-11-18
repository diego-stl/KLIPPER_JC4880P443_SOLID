#pragma once

#include "klipper_client.h"
#include "lvgl.h"

/* Called from the LVGL task. The saver covers the current view while it
 * continues to receive printer updates. */
bool portal_saver_active(void);
void portal_saver_show(lv_obj_t *layer, const klipper_snapshot_t *printer, int64_t now_us);
void portal_saver_hide(void);
void portal_saver_tick(int64_t now_us);
void portal_saver_update(const klipper_snapshot_t *printer);
