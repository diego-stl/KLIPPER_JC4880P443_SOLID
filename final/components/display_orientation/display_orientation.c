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
    const orientation_area_t full={0,0,ORIENTATION_UI_W-1,ORIENTATION_UI_H-1};
    return orientation_rotate_rgb565_area(rotation,source,dest,&full);
}
static bool area_valid(const orientation_area_t *a)
{
    return a&&a->x1>=0&&a->y1>=0&&a->x2>=a->x1&&a->y2>=a->y1&&
        a->x2<ORIENTATION_UI_W&&a->y2<ORIENTATION_UI_H;
}
static void area_join(orientation_area_t *a,const orientation_area_t *b)
{
    if(b->x1<a->x1)a->x1=b->x1;
    if(b->y1<a->y1)a->y1=b->y1;
    if(b->x2>a->x2)a->x2=b->x2;
    if(b->y2>a->y2)a->y2=b->y2;
}
bool orientation_damage_add(orientation_damage_t *damage,const orientation_area_t *area)
{
    if(!damage||!area_valid(area))return false;
    if(damage->pending)area_join(&damage->current,area);else damage->current=*area;
    damage->pending=true;return true;
}
bool orientation_damage_next(const orientation_damage_t *damage,orientation_area_t *area)
{
    if(!damage||!area||!damage->pending)return false;
    *area=damage->current;
    if(damage->have_previous)area_join(area,&damage->previous);
    return true;
}
void orientation_damage_commit(orientation_damage_t *damage)
{
    if(!damage||!damage->pending)return;
    damage->previous=damage->current;damage->have_previous=true;damage->pending=false;
}
bool orientation_rotate_rgb565_area(unsigned rotation,const uint16_t *source,uint16_t *dest,
                                   const orientation_area_t *area)
{
    if(!source||!dest||source==dest||!orientation_valid(rotation)||!area_valid(area))return false;
    // A 16x16 transpose tile keeps source/destination cache lines local in PSRAM.
    // Panel timing and DMA buffers stay natively 480x800.
    for(unsigned v0=area->y1;v0<=(unsigned)area->y2;v0+=16){
        unsigned vend=v0+16;if(vend>(unsigned)area->y2+1)vend=area->y2+1;
        for(unsigned u0=area->x1;u0<=(unsigned)area->x2;u0+=16){
            unsigned uend=u0+16;if(uend>(unsigned)area->x2+1)uend=area->x2+1;
            for(unsigned v=v0;v<vend;++v){
                for(unsigned u=u0;u<uend;++u){
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
