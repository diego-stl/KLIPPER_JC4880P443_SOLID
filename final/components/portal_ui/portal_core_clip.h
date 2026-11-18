#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "klipper_client.h"

typedef enum {
    PORTAL_CLIP_HIDDEN, PORTAL_CLIP_MAIN, PORTAL_CLIP_SAVER,
} portal_core_clip_view_t;

typedef enum {
    PORTAL_CLIP_NONE, PORTAL_CLIP_OFFLINE, PORTAL_CLIP_COMPLETE, PORTAL_CLIP_ERROR,
    PORTAL_CLIP_PAUSED, PORTAL_CLIP_PRINTING, PORTAL_CLIP_STANDBY, PORTAL_CLIP_COUNT,
} portal_core_clip_kind_t;

void portal_core_clip_start_load(void);
portal_core_clip_kind_t portal_core_clip_for_state(klipper_state_t state);
void portal_core_clip_select(portal_core_clip_kind_t kind);
lv_obj_t *portal_core_clip_create(lv_obj_t *parent, lv_obj_t *original);
bool portal_core_clip_update(lv_obj_t *image, portal_core_clip_view_t view,
                             portal_core_clip_kind_t kind, int64_t now_us);
void portal_core_clip_print_status(void);
void portal_core_clip_scan_card(void);
void portal_core_clip_set_format(const char *name);
void portal_core_clip_track_present(lv_display_t *display);
