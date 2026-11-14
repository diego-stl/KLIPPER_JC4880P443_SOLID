#pragma once

#include "lvgl.h"

typedef enum {
    PORTAL_CORE_BASE,
    PORTAL_CORE_COMPLETE,
    PORTAL_CORE_CANCELLED,
    PORTAL_CORE_OFFLINE,
    PORTAL_CORE_ERROR,
    PORTAL_CORE_WARNING,
} portal_core_variant_t;

lv_obj_t *portal_core_create(lv_obj_t *parent, portal_core_variant_t variant, int x, int y);
void portal_core_set_variant(lv_obj_t *core, portal_core_variant_t variant);
/* Pass the currently visible core or NULL when the visor is covered. */
void portal_core_tick(lv_obj_t *visible_core);

typedef enum {
    PORTAL_SAVER_FACE_BLUE,
    PORTAL_SAVER_FACE_GREEN,
    PORTAL_SAVER_FACE_YELLOW,
    PORTAL_SAVER_FACE_RED,
} portal_saver_face_t;

void portal_core_saver_create(lv_obj_t *parent, int x, int y);
void portal_core_saver_set_face(portal_saver_face_t face, int lid, bool first);
void portal_core_saver_tick(void);
void portal_core_saver_stop(void);
