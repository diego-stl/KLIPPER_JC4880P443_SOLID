#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
typedef enum {
    PORTAL_STARTUP, PORTAL_STANDBY, PORTAL_PRINTING, PORTAL_PAUSED,
    PORTAL_TOOLHEAD, PORTAL_FILES, PORTAL_MACROS, PORTAL_EXCLUDE,
    PORTAL_SETTINGS, PORTAL_ERROR, PORTAL_DETAILS, PORTAL_VIEW_COUNT
} portal_view_t;
// All calls, events and image/cache updates belong to the one LVGL task.
void portal_ui_init(lv_display_t *display, lv_indev_t *input);
void portal_ui_tick(void);

typedef struct {
    bool active, pending;
    portal_view_t view;
    uint32_t requested, applied;
} portal_inspection_status_t;
// Thread-safe console entry point: only queues requests/copies status. No LVGL.
bool portal_ui_console_command(const char *command);
void portal_ui_inspection_status(portal_inspection_status_t *status);
#ifdef PORTAL_UI_HOST_TEST
void portal_ui_test_show(portal_view_t view);
#endif
