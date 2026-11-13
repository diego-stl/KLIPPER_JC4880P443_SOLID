#pragma once

// Private console navigation. No transport, credentials, LVGL or service calls.
typedef enum {
    SERIAL_MENU_MAIN, SERIAL_MENU_DISPLAY, SERIAL_MENU_AUDIO, SERIAL_MENU_WIFI,
    SERIAL_MENU_KLIPPER, SERIAL_MENU_SYSTEM, SERIAL_MENU_LOGS,
    SERIAL_MENU_INSPECTION, SERIAL_MENU_COUNT
} serial_menu_t;

typedef enum {
    SERIAL_MENU_PASS, SERIAL_MENU_SHOW, SERIAL_MENU_COMMAND,
    SERIAL_MENU_NOTICE, SERIAL_MENU_CONFIRM_FORGET, SERIAL_MENU_INVALID,
    SERIAL_MENU_OPEN_INSPECTION
} serial_menu_action_t;

typedef struct {
    serial_menu_action_t action;
    // SHOW may carry a command to run before redrawing (leaving inspection).
    const char *text;
} serial_menu_result_t;

// Only call for complete command lines, never for SSID/password/host fields.
const char *serial_menu_normalise(char *command);
serial_menu_result_t serial_menu_route(serial_menu_t *menu, const char *command);
void serial_menu_print(serial_menu_t menu);
const char *serial_menu_prompt(serial_menu_t menu);
