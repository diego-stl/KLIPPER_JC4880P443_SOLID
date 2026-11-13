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
typedef struct { int x1,y1,x2,y2; } orientation_area_t;
// Both buffers retain full-frame strides. Only this logical rectangle is copied.
bool orientation_rotate_rgb565_area(unsigned rotation,const uint16_t *source,uint16_t *dest,
                                   const orientation_area_t *area);
typedef struct {
    orientation_area_t current,previous;
    bool pending,have_previous;
} orientation_damage_t;
bool orientation_damage_add(orientation_damage_t *damage,const orientation_area_t *area);
// The alternating LCD back buffer also needs the preceding frame's changes.
bool orientation_damage_next(const orientation_damage_t *damage,orientation_area_t *area);
void orientation_damage_commit(orientation_damage_t *damage);

typedef struct {
    bool blocked;
    bool frame_presented;
    bool fresh_release;
} orientation_gate_t;
void orientation_gate_begin(orientation_gate_t *gate);
void orientation_gate_frame(orientation_gate_t *gate, bool success);
void orientation_gate_sample(orientation_gate_t *gate, bool valid, bool fresh, bool pressed);
