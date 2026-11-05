#include "display_orientation.h"
_Static_assert(ORIENTATION_UI_W % 16 == 0 && ORIENTATION_UI_H % 16 == 0,
               "The fixed transpose tile must divide the logical frame");

static orientation_rotation_t s_rotation = ORIENTATION_R90;

bool orientation_valid(unsigned rotation)
{
    return rotation == ORIENTATION_R90 || rotation == ORIENTATION_R270;
}

orientation_rotation_t orientation_current(void) { return s_rotation; }

bool orientation_select(unsigned rotation)
{
    if (!orientation_valid(rotation)) return false;
    s_rotation = (orientation_rotation_t)rotation;
    return true;
}

bool orientation_map_touch(unsigned rotation, uint16_t raw_x, uint16_t raw_y,
                           uint16_t *u, uint16_t *v)
{
    if (!u || !v || !orientation_valid(rotation) ||
        raw_x >= ORIENTATION_PANEL_W || raw_y >= ORIENTATION_PANEL_H) return false;
    *u = rotation == ORIENTATION_R90 ? raw_y : ORIENTATION_PANEL_H - 1 - raw_y;
    *v = rotation == ORIENTATION_R90 ? ORIENTATION_PANEL_W - 1 - raw_x : raw_x;
    return true;
}

bool orientation_rotate_rgb565(unsigned rotation, const uint16_t *source, uint16_t *dest)
{
    if (!source || !dest || source == dest || !orientation_valid(rotation)) return false;
    // A 16x16 transpose tile keeps source/destination cache lines local in PSRAM.
    // Panel timing and DMA buffers stay natively 480x800.
    for (unsigned v0 = 0; v0 < ORIENTATION_UI_H; v0 += 16) {
        for (unsigned u0 = 0; u0 < ORIENTATION_UI_W; u0 += 16) {
            for (unsigned v = v0; v < v0 + 16; ++v) {
                for (unsigned u = u0; u < u0 + 16; ++u) {
                    size_t index = rotation == ORIENTATION_R90
                        ? (size_t)u * ORIENTATION_PANEL_W + (ORIENTATION_PANEL_W - 1 - v)
                        : (size_t)(ORIENTATION_PANEL_H - 1 - u) * ORIENTATION_PANEL_W + v;
                    dest[index] = source[(size_t)v * ORIENTATION_UI_W + u];
                }
            }
        }
    }
    return true;
}

void orientation_gate_begin(orientation_gate_t *gate)
{
    *gate = (orientation_gate_t){.blocked = true};
}
void orientation_gate_frame(orientation_gate_t *gate, bool success)
{
    if (gate->blocked && success) gate->frame_presented = true;
}
void orientation_gate_sample(orientation_gate_t *gate, bool valid, bool fresh, bool pressed)
{
    // Require a release observed AFTER presentation, never a latched old release.
    if (gate->blocked && gate->frame_presented && valid && fresh && !pressed) {
        gate->fresh_release = true;
        gate->blocked = false;
    }
}
