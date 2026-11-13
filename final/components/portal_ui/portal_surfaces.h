#pragma once

#include "lvgl.h"
#include "portal_assets.h"

#define PORTAL_BACKGROUND_COLOR 0xD9D9D9
#define PORTAL_PANEL_COLOR 0xEEEEEE
#define PORTAL_MEDIUM_COLOR 0x7F7F7F
#define PORTAL_STATUS_COLOR 0x6D7074
#define PORTAL_BUTTON_GRAY 0xBFBFBF
#define PORTAL_TEXT_COLOR 0x202429
#define PORTAL_LIGHT_TEXT 0xF6FAFF
#define PORTAL_PALE_BLUE 0xE0F6FF
#define PORTAL_PALE_ORANGE 0xFFF0D8
#define PORTAL_PALE_RED 0xFFE0E4
#define PORTAL_BLUE 0x00A6FF
#define PORTAL_ORANGE 0xFF8A1C
#define PORTAL_RED 0xD94A3A
#define PORTAL_DARK_CARD_COLOR 0x202429

/* Disabled text buttons keep the original translucent style. Restore their
 * dark backing in the parent's paint pass so a light panel cannot wash them
 * out. This adds neither widgets nor changes to the protected control styles. */
static inline void portal_text_backdrops_draw(lv_event_t *event)
{
    lv_obj_t *surface=lv_event_get_target(event);lv_area_t bounds;lv_obj_get_coords(surface,&bounds);
    /* Some existing panels and their controls are siblings. Paint on the
     * containing panel as well, after its fill and before those controls. */
    for(unsigned pass=0;pass<2;++pass){
        lv_obj_t *parent=pass?lv_obj_get_parent(surface):surface;if(!parent)continue;
        for(unsigned i=0;i<lv_obj_get_child_count(parent);++i){
            lv_obj_t *child=lv_obj_get_child(parent,i);lv_area_t area;lv_obj_get_coords(child,&area);
            if(!lv_obj_check_type(child,&lv_button_class)||lv_obj_has_flag(child,LV_OBJ_FLAG_HIDDEN))continue;
            /* Include the theme's fade back to enabled, after DISABLED clears. */
            if(lv_obj_get_style_bg_opa(child,0)==LV_OPA_COVER&&lv_obj_get_style_opa(child,0)==LV_OPA_COVER)continue;
            if(area.x1<bounds.x1||area.y1<bounds.y1||area.x2>bounds.x2||area.y2>bounds.y2)continue;
            bool image=false;
            for(unsigned j=0;j<lv_obj_get_child_count(child);++j)
                if(lv_obj_check_type(lv_obj_get_child(child,j),&lv_image_class))image=true;
            if(image)continue;
            lv_draw_rect_dsc_t backing;lv_draw_rect_dsc_init(&backing);
            backing.bg_color=lv_color_hex(PORTAL_DARK_CARD_COLOR);backing.bg_opa=LV_OPA_COVER;backing.radius=7;
            lv_draw_rect(lv_event_get_layer(event),&backing,&area);
        }
    }
}
static inline void portal_preserve_text_backdrops(lv_obj_t *parent)
{
    lv_obj_add_event_cb(parent,portal_text_backdrops_draw,LV_EVENT_DRAW_MAIN,NULL);
}

static inline void portal_background(lv_obj_t *object)
{
    lv_obj_set_style_bg_image_src(object,&portal_background_image,0);
    portal_preserve_text_backdrops(object);
}

/* The inset is painted rather than changing border width or padding: LVGL
 * therefore keeps every label, image, content origin and touch area exact. */
static inline void portal_icon_border_draw(lv_event_t *event)
{
    lv_obj_t *object=lv_event_get_target(event);lv_area_t area;lv_obj_get_coords(object,&area);
    int inset=lv_obj_get_height(object)>=120?4:2;
    area.x1+=inset;area.y1+=inset;area.x2-=inset;area.y2-=inset;
    lv_draw_rect_dsc_t border;lv_draw_rect_dsc_init(&border);
    border.bg_opa=LV_OPA_TRANSP;border.border_opa=LV_OPA_COVER;
    border.border_color=lv_color_hex((uint32_t)(uintptr_t)lv_event_get_user_data(event));
    border.border_width=lv_obj_get_height(object)>=120?3:2;border.radius=4;
    lv_draw_rect(lv_event_get_layer(event),&border,&area);
}
