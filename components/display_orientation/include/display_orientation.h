#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { ORIENTATION_PANEL_W = 480, ORIENTATION_PANEL_H = 800,
       ORIENTATION_UI_W = 800, ORIENTATION_UI_H = 480 };
typedef enum { ORIENTATION_R90 = 90, ORIENTATION_R270 = 270 } orientation_rotation_t;

bool orientation_valid(unsigned rotation);
// State is read/written exclusively by the task that owns LVGL and GT911 polling.
orientation_rotation_t orientation_current(void);
bool orientation_select(unsigned rotation);
bool orientation_map_touch(unsigned rotation, uint16_t raw_x, uint16_t raw_y,
                           uint16_t *u, uint16_t *v);
bool orientation_rotate_rgb565(unsigned rotation, const uint16_t *source, uint16_t *dest);

typedef struct {
    bool blocked;
    bool frame_presented;
    bool fresh_release;
} orientation_gate_t;
void orientation_gate_begin(orientation_gate_t *gate);
void orientation_gate_frame(orientation_gate_t *gate, bool success);
void orientation_gate_sample(orientation_gate_t *gate, bool valid, bool fresh, bool pressed);
