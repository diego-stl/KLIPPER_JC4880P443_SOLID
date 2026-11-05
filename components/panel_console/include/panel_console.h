#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
// Console command dispatcher; never consumes UI command results or calls LVGL.
bool panel_console_command(const char *command);
#ifdef __cplusplus
}
#endif
