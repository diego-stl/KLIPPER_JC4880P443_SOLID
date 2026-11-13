#include "portal_ui.h"
#include "portal_assets.h"
#include "device_network.h"
#include "klipper_client.h"
#include "board_display.h"
#include "audio_service.h"
#include "audio_events.h"
#include "portal_network_settings.h"
#include "portal_saver.h"
#include "portal_surfaces.h"
#include "src/draw/lv_image_decoder_private.h"
#include "esp_timer.h"
#include "esp_log.h"
#ifndef PORTAL_UI_HOST_TEST
#include "esp_app_desc.h"
#endif
#include "freertos/FreeRTOS.h"
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PANEL 0x081418
#define BUTTON 0x182431
#define BORDER 0x284b66
#define MUTED 0xacc2de
#define WHITE PORTAL_LIGHT_TEXT
#define CYAN 0x00c9f4
#define ORANGE 0xff9d00
#define GREEN 0x00e676
#define RED 0xff304d
#define YELLOW 0xffd740
#define STARTUP_GOLD 0xEEAE2E
#define STARTUP_YELLOW 0xFFFF00
#define STARTUP_INK 0x17130B
#define FILAMENT_STEP_MM 50
#define SAVER_BRIGHTNESS 25

enum {
    DO_BACK=1,DO_EMERGENCY,DO_PAUSE,DO_RESUME,DO_CANCEL,DO_PRINT,
    DO_MACRO,DO_EXCLUDE,DO_RESTART,DO_SHUTDOWN,DO_EXTRUDE,DO_RETRACT,
    DO_EDIT_HOTEND,DO_EDIT_BED,DO_MATERIAL_APPLY,DO_COOL,DO_HOME_X,DO_HOME_Y,
    DO_HOME_Z,DO_HOME_ALL,DO_MOTORS_OFF,DO_X_MINUS,DO_X_PLUS,DO_Y_MINUS,
    DO_Y_PLUS,DO_Z_MINUS,DO_Z_PLUS,DO_DETAILS,DO_AUDIO_TEST,DO_LAST_JOB,DO_RESET_JOB,DO_NETWORK,DO_TUNING,
    DO_VIEW=100,DO_FILE=200,DO_MACRO_SELECT=300,DO_OBJECT=400
};
typedef struct {
    lv_obj_t *image,*empty;
    int width,height;
    const void *source;
} preview_t;
typedef struct { lv_obj_t *current,*target; } temperature_t;
static lv_display_t *s_display;
static lv_indev_t *s_input;
enum { NAV_PENDING, NAV_RIGHT, NAV_OTHER };
enum { NAV_TAP_SLOP=12, NAV_BACK_DISTANCE=80 };
static struct {
    lv_obj_t *owner;
    lv_point_t start,delta;
    portal_view_t view;
    unsigned direction;
    bool active,moved;
} s_navigation;
static lv_obj_t *s_screens[PORTAL_VIEW_COUNT];
static portal_view_t s_view=PORTAL_STARTUP,s_previous=PORTAL_STANDBY;
static klipper_snapshot_t s_printer;
static device_network_snapshot_t s_network;
static klipper_metadata_t *s_metadata,*s_last_metadata;
static klipper_last_job_t s_last_job;
static lv_image_dsc_t s_thumbnail,s_last_thumbnail;
static bool s_have_connected,s_adjusting_brightness,s_print_pending,s_reset_pending,s_details_last;
static bool s_print_uncertain;
static uint32_t s_print_pending_job;
static char s_print_pending_file[256];
static uint32_t s_seen_generation,s_seen_metadata;
static int64_t s_next_tick,s_toast_until;
static lv_obj_t *s_toast,*s_toast_text,*s_modal;
static unsigned s_modal_action;
static uint32_t s_modal_generation;
static char s_modal_file[256];
static uint32_t s_tuning_job;
static klipper_state_t s_tuning_state,s_tuning_raw_state;
static portal_view_t s_tuning_view;
static unsigned s_tuning_draft[3];
static bool s_tuning_valid[3],s_tuning_pending[3];
static lv_obj_t *s_tuning_actual[3],*s_tuning_proposed[3],*s_tuning_minus[3],*s_tuning_plus[3],*s_tuning_apply[3];
static const unsigned TUNING_MIN[]={10,50,0},TUNING_MAX[]={200,150,100};
static lv_obj_t *s_target_text;
static char s_target_digits[8];
static bool s_target_bed;
static uint8_t s_brightness=80;
static uint8_t s_brightness_before_adjustment=80;
static uint16_t s_sleep_minutes=10;
static temperature_t s_main_temps[3][2];
static preview_t s_previews[4];
static lv_obj_t *s_standby_core;
static lv_obj_t *s_progress_text[2],*s_print_file[2],*s_print_state[2];
static lv_obj_t *s_start_status,*s_start_log,*s_start_wifi,*s_start_klipper;
static lv_obj_t *s_standby_status;
static lv_obj_t *s_tool_current[2],*s_tool_target[2],*s_axis_position[3],*s_material,*s_step;
static lv_obj_t *s_brightness_slider,*s_brightness_text,*s_timeout,*s_network_text;
static lv_obj_t *s_audio_switch,*s_volume_slider,*s_volume_text,*s_audio_test,*s_audio_status;
static lv_obj_t *s_audio_state_icon;
static bool s_adjusting_volume;
static lv_obj_t *s_files_list,*s_macros_list,*s_objects_list,*s_map;
static lv_obj_t *s_file_title,*s_file_tools,*s_file_print;
static lv_obj_t *s_macro_name,*s_macro_run,*s_exclude_run,*s_map_info;
static lv_obj_t *s_details_file,*s_details_time,*s_details_weight,*s_details_colors,*s_details_result,*s_details_reset;
static lv_obj_t *s_file_buttons[KLIPPER_FILES_MAX];
static bool s_adjustment_screen;
static lv_obj_t *s_error_text;
static char s_selected_file[256],s_selected_macro[128],s_selected_object[128];
static char s_last_job_file[256];
static char s_files_cache[KLIPPER_FILES_MAX][256],s_macros_cache[KLIPPER_MACROS_MAX][128];
static klipper_object_t s_objects_cache[KLIPPER_OBJECTS_MAX];
static unsigned s_files_count=~0U,s_macros_count=~0U,s_objects_count=~0U;
static unsigned s_material_index,s_step_index=2;
static bool s_map_bounds_valid;
static float s_map_bounds[4];
static const unsigned STEPS[]={1,5,10,20,50,100};
static const unsigned MATERIAL_HOTEND[]={170,200,170,255},MATERIAL_BED[]={55,65,55,100};
static const char *const VIEW_NAMES[PORTAL_VIEW_COUNT]={
    "startup","standby","printing","paused","toolhead","files","macros",
    "exclude","settings","error","details"
};
// Only the GUI owner changes s_inspecting and LVGL objects. The console owns
// neither; it publishes a bounded last-request-wins mailbox under a short lock.
static bool s_inspecting,s_inspection_request_pending;
static int s_inspection_target;
static portMUX_TYPE s_inspection_lock=portMUX_INITIALIZER_UNLOCKED;
static portal_inspection_status_t s_inspection_status;
static lv_obj_t *s_inspection_shield,*s_inspection_label;
static void show_view(portal_view_t view);
static portal_view_t live_view(void);
static void click(lv_event_t *event);
static void navigation_pressing(lv_event_t *event);
static void update_all(void);
static void inspection_process_request(void);
static void inspection_end(const char *reason);

static bool inspection_input_blocked(void)
{
    if(s_inspecting)return true;
    portENTER_CRITICAL(&s_inspection_lock);
    bool blocked=s_inspection_request_pending&&s_inspection_target>=0;
    portEXIT_CRITICAL(&s_inspection_lock);
    return blocked;
}
static bool settings_input_blocked(void) { return portal_saver_active()||s_modal||inspection_input_blocked(); }
static void apply_brightness(uint8_t value)
{
    board_display_set_brightness(portal_saver_active()&&value>SAVER_BRIGHTNESS?SAVER_BRIGHTNESS:value);
}
static void wake_display(void)
{
    if(!portal_saver_active())return;
    portal_saver_hide();
    apply_brightness(s_brightness);
    lv_display_trigger_activity(s_display);
}
static bool movement_blocked(void)
{
    return s_print_pending||s_printer.raw_state==KLIPPER_PRINTING||s_printer.raw_state==KLIPPER_PAUSED||
        s_printer.state==KLIPPER_PRINTING||s_printer.state==KLIPPER_PAUSED;
}
static void inspection_publish(void)
{
    portENTER_CRITICAL(&s_inspection_lock);
    s_inspection_status.active=s_inspecting;s_inspection_status.view=s_view;
    portEXIT_CRITICAL(&s_inspection_lock);
}
void portal_ui_inspection_status(portal_inspection_status_t *status)
{
    if(!status)return;
    portENTER_CRITICAL(&s_inspection_lock);
    *status=s_inspection_status;
    status->pending=status->requested!=status->applied;
    portEXIT_CRITICAL(&s_inspection_lock);
}
bool portal_ui_console_command(const char *command)
{
    if(!command||(strncmp(command,"screen",6)!=0)||(command[6]&&command[6]!=' '))return false;
    if(!strcmp(command,"screen status")){
        portal_inspection_status_t status;portal_ui_inspection_status(&status);
        printf("SCREEN_STATUS active=%d view=%s pending=%d requested=%"PRIu32" applied=%"PRIu32"\n",
            status.active,VIEW_NAMES[status.view],status.pending,status.requested,status.applied);
        return true;
    }
    if(!strcmp(command,"screen")||!strcmp(command,"screen help")||!strcmp(command,"screen list")){
        printf("screen show NOMBRE | screen live | screen status\n");
        for(unsigned i=0;i<PORTAL_VIEW_COUNT;++i)printf("SCREEN_VIEW %u %s\n",i+1,VIEW_NAMES[i]);
        return true;
    }
    int target=-1;
    if(strcmp(command,"screen live")){
        if(strncmp(command,"screen show ",12)){
            printf("Uso: screen show NOMBRE | screen live | screen status\n");return true;
        }
        for(target=0;target<PORTAL_VIEW_COUNT;++target)if(!strcmp(command+12,VIEW_NAMES[target]))break;
        if(target==PORTAL_VIEW_COUNT){printf("SCREEN_ERROR vista invalida; use screen list\n");return true;}
    }
    portENTER_CRITICAL(&s_inspection_lock);
    s_inspection_target=target;s_inspection_request_pending=true;
    uint32_t id=++s_inspection_status.requested;
    portEXIT_CRITICAL(&s_inspection_lock);
    printf("SCREEN_REQUEST id=%"PRIu32" view=%s\n",id,target<0?"live":VIEW_NAMES[target]);
    return true;
}

static void set_text(lv_obj_t *object,const char *value)
{
    if(object && strcmp(lv_label_get_text(object),value))lv_label_set_text(object,value);
}
static void text_color(lv_obj_t *object,uint32_t color)
{
    lv_color_t value=lv_color_hex(color);
    if(!lv_color_eq(lv_obj_get_style_text_color(object,0),value))lv_obj_set_style_text_color(object,value,0);
}
static void visible(lv_obj_t *object,bool show)
{
    if(lv_obj_has_flag(object,LV_OBJ_FLAG_HIDDEN)!=show)return;
    if(show)lv_obj_remove_flag(object,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(object,LV_OBJ_FLAG_HIDDEN);
}
static void format(lv_obj_t *object,const char *fmt,...)
{
    char buffer[512];va_list args;va_start(args,fmt);vsnprintf(buffer,sizeof(buffer),fmt,args);va_end(args);set_text(object,buffer);
}
static void style_surface(lv_obj_t *object,uint32_t fill,uint32_t border,int radius)
{
    lv_obj_set_style_bg_color(object,lv_color_hex(fill),0);lv_obj_set_style_bg_opa(object,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(object,1,0);lv_obj_set_style_border_color(object,lv_color_hex(border),0);
    lv_obj_set_style_radius(object,radius,0);lv_obj_set_style_shadow_width(object,0,0);lv_obj_set_style_pad_all(object,0,0);
}
static lv_obj_t *panel(lv_obj_t *parent,int x,int y,int w,int h)
{
    lv_obj_t *p=lv_obj_create(parent);lv_obj_set_pos(p,x,y);lv_obj_set_size(p,w,h);
    style_surface(p,PANEL,BORDER,8);lv_obj_remove_flag(p,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(p,navigation_pressing,LV_EVENT_PRESSING,NULL);return p;
}
static lv_obj_t *colored_panel(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color)
{
    lv_obj_t *p=panel(parent,x,y,w,h);
    lv_obj_set_style_bg_color(p,lv_color_hex(color),0);
    lv_obj_set_style_border_color(p,lv_color_hex(0x929DA2),0);
    lv_obj_set_style_bg_grad_dir(p,LV_GRAD_DIR_NONE,0);
    portal_preserve_text_backdrops(p);
    return p;
}
static lv_obj_t *large_panel(lv_obj_t *parent,int x,int y,int w,int h)
{
    return colored_panel(parent,x,y,w,h,PORTAL_PANEL_COLOR);
}
static lv_obj_t *dark_card(lv_obj_t *parent,int x,int y,int w,int h)
{
    lv_obj_t *p=panel(parent,x,y,w,h);
    lv_obj_set_style_bg_color(p,lv_color_hex(PORTAL_DARK_CARD_COLOR),0);
    lv_obj_set_style_bg_grad_dir(p,LV_GRAD_DIR_NONE,0);return p;
}
static lv_obj_t *label(lv_obj_t *parent,const char *value,int x,int y,int w,const lv_font_t *font,uint32_t color)
{
    lv_obj_t *p=lv_label_create(parent);lv_label_set_text(p,value);lv_obj_set_pos(p,x,y);lv_obj_set_width(p,w);
    lv_obj_set_style_text_font(p,portal_font(font),0);lv_obj_set_style_text_color(p,lv_color_hex(color),0);
    lv_obj_remove_flag(p,LV_OBJ_FLAG_CLICKABLE);return p;
}
static lv_obj_t *center_label(lv_obj_t *parent,const char *value,int x,int y,int w,const lv_font_t *font,uint32_t color)
{
    lv_obj_t *p=label(parent,value,x,y,w,font,color);lv_obj_set_style_text_align(p,LV_TEXT_ALIGN_CENTER,0);return p;
}
static lv_obj_t *icon(lv_obj_t *parent,const char *name,int x,int y,unsigned size,uint32_t color)
{
    const lv_image_dsc_t *source=portal_icon_for_symbol(name);
    if(!source||!source->header.w)return NULL;
    lv_obj_t *image=lv_image_create(parent);lv_image_set_src(image,source);lv_image_set_pivot(image,0,0);
    lv_image_set_scale(image,size*256/source->header.w);lv_obj_set_pos(image,x,y);
    lv_obj_set_style_image_recolor(image,lv_color_hex(color),0);lv_obj_set_style_image_recolor_opa(image,LV_OPA_COVER,0);
    lv_obj_remove_flag(image,LV_OBJ_FLAG_CLICKABLE);return image;
}
static const lv_font_t *button_font(const char *text,int width,bool compact)
{
    if(!compact&&lv_text_get_width(text,strlen(text),portal_font(&lv_font_montserrat_20),0)<=width)return &lv_font_montserrat_20;
    if(lv_text_get_width(text,strlen(text),portal_font(&lv_font_montserrat_16),0)<=width)return &lv_font_montserrat_16;
    return &lv_font_montserrat_14;
}
static lv_obj_t *button(lv_obj_t *parent,const char *value,const char *symbol,int x,int y,int w,int h,uint32_t color,unsigned action)
{
    lv_obj_t *b=lv_button_create(parent);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,h);style_surface(b,PANEL,BORDER,7);
    lv_obj_set_style_bg_color(b,lv_color_hex(0x073447),LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b,LV_OPA_40,LV_STATE_DISABLED);
    lv_obj_set_style_border_color(b,lv_color_hex(color),LV_STATE_FOCUSED);
    lv_obj_set_ext_click_area(b,h<48?(48-h)/2:0);
    lv_obj_add_event_cb(b,navigation_pressing,LV_EVENT_PRESSING,NULL);
    lv_obj_add_event_cb(b,click,LV_EVENT_CLICKED,(void*)(uintptr_t)action);
    bool illustrated=symbol&&*symbol&&strcmp(symbol,"backspace");
    bool startup=parent==s_screens[PORTAL_STARTUP];
    bool main_grid=w==208&&h==144;
    bool error_action=action==DO_RESTART||action==DO_SHUTDOWN;
    bool error_secondary=parent==s_screens[PORTAL_ERROR]&&action!=DO_RESTART;
    bool jog=action>=DO_X_MINUS&&action<=DO_Z_PLUS;
    if(startup){
        color=STARTUP_YELLOW;
        style_surface(b,STARTUP_GOLD,color,4);
        lv_obj_set_style_bg_color(b,lv_color_hex(0xD29320),LV_STATE_PRESSED);
    }else if(illustrated){
        color=(!strcmp(symbol,"home")||!strcmp(symbol,"print")||!strcmp(symbol,"pause")||action==DO_RESUME)?PORTAL_ORANGE:
            (!strcmp(symbol,"stop")||!strcmp(symbol,"cancel")||!strcmp(symbol,"power"))?PORTAL_RED:PORTAL_BLUE;
        uint32_t fill=error_secondary?PORTAL_STATUS_COLOR:jog||(main_grid&&
            ((x==360&&y!=168)||(x==580&&y==168)))?PORTAL_BUTTON_GRAY:0xFFFFFF;
        lv_obj_set_style_bg_color(b,lv_color_hex(fill),0);
        lv_obj_set_style_bg_color(b,lv_color_hex(0xDCEEF5),LV_STATE_PRESSED);
        lv_obj_set_style_border_color(b,lv_color_hex(0x929DA2),0);
        lv_obj_set_style_border_color(b,lv_color_hex(color),LV_STATE_FOCUSED);
        lv_obj_add_event_cb(b,portal_icon_border_draw,LV_EVENT_DRAW_MAIN,(void*)(uintptr_t)color);
    }
    if(symbol && *symbol){
        unsigned size=h>=120?64:32;
        uint32_t ink=startup?STARTUP_INK:illustrated&&!error_secondary?PORTAL_TEXT_COLOR:WHITE;
        if(!*value){icon(b,symbol,(w-size)/2,(h-size)/2,size,color);}
        else if(jog){
            bool minus=(action-DO_X_MINUS)%2==0;
            icon(b,symbol,minus?6:38,18,size,color);
            center_label(b,value,minus?38:6,24,32,&lv_font_montserrat_16,ink);
        }
        else if(h<64){
            icon(b,symbol,12,(h-size)/2,size,color);
            const lv_font_t *font=button_font(value,w-68,false);
            center_label(b,value,56,(h-lv_font_get_line_height(font))/2,w-68,font,ink);
        }else{
            icon(b,symbol,(w-size)/2,main_grid?30:error_action?15:h>=120?14:startup?4:6,size,color);
            bool multiline=strchr(value,'\n')!=NULL;
            const lv_font_t *font=multiline?&lv_font_montserrat_20:button_font(value,w-12,h<80);
            center_label(b,value,6,h-lv_font_get_line_height(font)*(multiline?2:1)-(startup?6:8),w-12,font,ink);
        }
    }else{lv_obj_t *t=center_label(b,value,6,0,w-12,&lv_font_montserrat_20,WHITE);lv_obj_center(t);}
    return b;
}
static void enabled(lv_obj_t *object,bool value)
{
    if(value)lv_obj_remove_state(object,LV_STATE_DISABLED);else lv_obj_add_state(object,LV_STATE_DISABLED);
}
static void toast(const char *message)
{
    set_text(s_toast_text,message);lv_obj_remove_flag(s_toast,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(s_toast);
    s_toast_until=esp_timer_get_time()+2500000;
}
static bool navigation_control(lv_obj_t *object)
{
    for(;object;object=lv_obj_get_parent(object)){
        if(lv_obj_check_type(object,&lv_slider_class)||lv_obj_check_type(object,&lv_switch_class)||
           lv_obj_check_type(object,&lv_dropdown_class)||lv_obj_check_type(object,&lv_textarea_class))return true;
    }
    return false;
}
static bool navigation_secondary(void)
{
    return s_view==PORTAL_TOOLHEAD||s_view==PORTAL_FILES||s_view==PORTAL_MACROS||
        s_view==PORTAL_EXCLUDE||s_view==PORTAL_SETTINGS||s_view==PORTAL_DETAILS;
}
static void navigation_update(lv_indev_t *input)
{
    if(!s_navigation.active)return;
    if(s_navigation.view!=s_view||settings_input_blocked()){s_navigation.active=false;return;}
    lv_point_t point;lv_indev_get_point(input,&point);
    int dx=point.x-s_navigation.start.x,dy=point.y-s_navigation.start.y;
    s_navigation.delta.x=dx;s_navigation.delta.y=dy;
    if(dx*dx+dy*dy>NAV_TAP_SLOP*NAV_TAP_SLOP)s_navigation.moved=true;
    /* Once a list starts scrolling, this contact can never become Back. */
    if(lv_indev_get_scroll_obj(input))s_navigation.direction=NAV_OTHER;
    if(s_navigation.moved&&s_navigation.direction==NAV_PENDING){
        if(dx>0&&dx>=2*abs(dy))s_navigation.direction=NAV_RIGHT;
        else if(dx<0||abs(dy)>=abs(dx))s_navigation.direction=NAV_OTHER;
    }
}
static void navigation_pressing(lv_event_t *event)
{
    lv_indev_t *input=lv_indev_active();
    if(input&&lv_event_get_target(event)==s_navigation.owner)navigation_update(input);
}
static void create_screen(portal_view_t view)
{
    uint32_t color=view==PORTAL_STARTUP?STARTUP_GOLD:PORTAL_BACKGROUND_COLOR;
    lv_obj_t *screen=lv_obj_create(NULL);s_screens[view]=screen;style_surface(screen,color,color,0);
    lv_obj_set_style_bg_grad_dir(screen,LV_GRAD_DIR_NONE,0);
    if(view!=PORTAL_STARTUP)portal_background(screen);
    lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(screen,LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(screen,navigation_pressing,LV_EVENT_PRESSING,NULL);
}
static lv_obj_t *artwork(lv_obj_t *parent,const lv_image_dsc_t *source,int x,int y)
{
    lv_obj_t *image=lv_image_create(parent);
    lv_image_set_src(image,source);lv_obj_set_pos(image,x,y);
    lv_obj_remove_flag(image,LV_OBJ_FLAG_CLICKABLE);
    return image;
}
static void create_preview(preview_t *preview,lv_obj_t *parent,int x,int y,int w,int h)
{
    lv_obj_t *root=lv_obj_create(parent);lv_obj_remove_style_all(root);
    lv_obj_set_pos(root,x,y);lv_obj_set_size(root,w,h);preview->width=w;preview->height=h;
    lv_obj_remove_flag(root,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    preview->image=lv_image_create(root);lv_obj_remove_flag(preview->image,LV_OBJ_FLAG_CLICKABLE);
    preview->empty=center_label(root,"NO DATA",0,h-22,w,&lv_font_montserrat_16,PORTAL_TEXT_COLOR);
}
static void update_preview(preview_t *preview,const lv_image_dsc_t *thumbnail,bool loading)
{
    const lv_image_dsc_t *source=thumbnail&&thumbnail->data_size?thumbnail:&portal_aperture_image;
    bool valid=source!=&portal_aperture_image;
    if(preview->source!=source){
        preview->source=source;lv_image_set_src(preview->image,source);lv_image_set_pivot(preview->image,0,0);
        unsigned area=valid?preview->height:preview->height-32;
        unsigned sx=((unsigned)preview->width*256+source->header.w-1)/source->header.w,sy=(area*256+source->header.h-1)/source->header.h;
        unsigned scale=sx<sy?sx:sy;
        lv_image_set_scale(preview->image,scale);
        lv_obj_set_pos(preview->image,(preview->width-source->header.w*scale/256)/2,(area-source->header.h*scale/256)/2);
    }
    visible(preview->empty,!valid);
    if(!valid)set_text(preview->empty,loading?"Cargando...":"NO DATA");
}

static temperature_t temperature(lv_obj_t *parent,int x,int y,int w,const char *name,const char *symbol)
{
    lv_obj_t *p=panel(parent,x,y,w,52);
    icon(p,symbol,10,10,32,ORANGE);
    label(p,name,52,3,w-60,&lv_font_montserrat_14,MUTED);
    temperature_t t={label(p,"--",52,19,90,&lv_font_montserrat_28,WHITE),label(p,"/ --",w-112,26,102,&lv_font_montserrat_16,ORANGE)};
    lv_obj_set_style_text_align(t.target,LV_TEXT_ALIGN_RIGHT,0);return t;
}
static void create_startup(void)
{
    lv_obj_t *s=s_screens[PORTAL_STARTUP];
    lv_obj_t *history=panel(s,10,10,376,458);
    style_surface(history,STARTUP_GOLD,STARTUP_YELLOW,11);
    lv_obj_add_flag(history,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(history,LV_DIR_VER);
    lv_obj_set_style_pad_bottom(history,12,0);
    label(history,"HISTORIAL DE CONEXIÓN",11,17,348,&lv_font_montserrat_16,STARTUP_INK);
    s_start_log=label(history,"",11,54,348,&lv_font_montserrat_16,STARTUP_INK);
    lv_obj_set_style_text_line_space(s_start_log,5,0);

    lv_obj_t *credits=panel(s,396,10,270,118);
    style_surface(credits,STARTUP_GOLD,STARTUP_YELLOW,3);
    label(credits,"KLIPPER_JC4880P443",8,6,252,&lv_font_montserrat_16,STARTUP_INK);
#ifdef PORTAL_UI_HOST_TEST
    const char *version="Simulador";
#else
    const char *version=esp_app_get_description()->version;
#endif
    lv_obj_t *credit_text=label(credits,"",8,26,252,&lv_font_montserrat_14,STARTUP_INK);
    format(credit_text,"Versión: %s\nAutor: Demorian_Omega",version);
    lv_obj_t *aperture=lv_image_create(s);
    lv_image_set_src(aperture,&portal_start_aperture_image);
    lv_obj_set_pos(aperture,670,10);

    lv_obj_t *status=panel(s,400,247,392,41);
    style_surface(status,STARTUP_GOLD,STARTUP_YELLOW,4);
    s_start_status=label(status,"Conectando Wi-Fi...",11,7,366,&lv_font_montserrat_20,STARTUP_INK);
    lv_obj_set_height(s_start_status,24);
    lv_label_set_long_mode(s_start_status,LV_LABEL_LONG_DOT);

    lv_obj_t *wifi=panel(s,409,313,114,155),*klipper=panel(s,532,313,114,155);
    style_surface(wifi,STARTUP_GOLD,STARTUP_YELLOW,18);
    style_surface(klipper,STARTUP_GOLD,STARTUP_YELLOW,18);
    lv_obj_t *wifi_image=lv_image_create(wifi);
    lv_image_set_src(wifi_image,&portal_start_wifi_image);
    lv_obj_set_pos(wifi_image,18,21);
    lv_obj_t *klipper_image=lv_image_create(klipper);
    lv_image_set_src(klipper_image,&portal_klipper_image);
    lv_obj_set_pos(klipper_image,6,27);
    s_start_wifi=center_label(wifi,"Conectando",3,115,106,&lv_font_montserrat_14,STARTUP_INK);
    s_start_klipper=center_label(klipper,"Esperando",3,115,106,&lv_font_montserrat_14,STARTUP_INK);
    button(s,"AJUSTES","settings",668,404,122,65,STARTUP_YELLOW,DO_VIEW+PORTAL_SETTINGS);
}
static void create_standby(void)
{
    lv_obj_t *s=s_screens[PORTAL_STANDBY];
    s_standby_core=artwork(s,&portal_core_base,39,10);
    colored_panel(s,12,319,332,149,PORTAL_STATUS_COLOR);
    s_standby_status=center_label(s,"Reposo",26,323,304,&lv_font_montserrat_20,PORTAL_PALE_BLUE);
    s_main_temps[0][0]=temperature(s,26,350,304,"BOQUILLA","nozzle");
    s_main_temps[0][1]=temperature(s,26,408,304,"CAMA","bed");
    button(s,"IMPRIMIR","print",360,12,208,144,GREEN,DO_VIEW+PORTAL_FILES);
    button(s,"Control manual","move",580,12,208,144,CYAN,DO_VIEW+PORTAL_TOOLHEAD);
    button(s,"ÚLTIMO TRABAJO","last_job",360,168,208,144,CYAN,DO_LAST_JOB);
    button(s,"AJUSTES","settings",580,168,208,144,CYAN,DO_VIEW+PORTAL_SETTINGS);
    button(s,"MACROS","macro",360,324,208,144,CYAN,DO_VIEW+PORTAL_MACROS);
    button(s,"PARADA","stop",580,324,208,144,RED,DO_EMERGENCY);
}
static void create_printing(bool paused)
{
    unsigned i=paused?1:0;lv_obj_t *s=s_screens[paused?PORTAL_PAUSED:PORTAL_PRINTING];
    lv_obj_t *preview=large_panel(s,12,10,332,247);
    lv_obj_t *status=colored_panel(s,12,268,332,200,PORTAL_STATUS_COLOR);
    lv_obj_add_event_cb(preview,click,LV_EVENT_CLICKED,(void*)(uintptr_t)DO_DETAILS);
    lv_obj_add_event_cb(status,click,LV_EVENT_CLICKED,(void*)(uintptr_t)DO_DETAILS);
    s_print_file[i]=label(preview,"---",14,12,304,&lv_font_montserrat_16,PORTAL_TEXT_COLOR);lv_label_set_long_mode(s_print_file[i],LV_LABEL_LONG_SCROLL_CIRCULAR);
    create_preview(&s_previews[i],preview,66,42,200,200);
    s_print_state[i]=label(status,paused?"EN PAUSA":"IMPRIMIENDO",14,8,304,&lv_font_montserrat_20,PORTAL_PALE_ORANGE);
    label(status,"PROGRESO",14,44,170,&lv_font_montserrat_14,WHITE);
    s_progress_text[i]=label(status,"-- %",198,37,120,&lv_font_montserrat_28,PORTAL_PALE_ORANGE);lv_obj_set_style_text_align(s_progress_text[i],LV_TEXT_ALIGN_RIGHT,0);
    s_main_temps[i+1][0]=temperature(s,26,350,304,"BOQUILLA","nozzle");
    s_main_temps[i+1][1]=temperature(s,26,408,304,"CAMA","bed");
    button(s,paused?"REANUDAR":"PAUSAR",paused?"play":"pause",360,12,208,144,paused?YELLOW:ORANGE,paused?DO_RESUME:DO_PAUSE);
    button(s,"CANCELAR","cancel",580,12,208,144,RED,DO_CANCEL);
    char extrude[24],retract[24];
    snprintf(extrude,sizeof(extrude),"EXTRUIR %d mm",FILAMENT_STEP_MM);
    snprintf(retract,sizeof(retract),"RETRAER %d mm",FILAMENT_STEP_MM);
    button(s,paused?extrude:"MACROS",paused?"extrude":"macro",360,168,208,144,CYAN,paused?DO_EXTRUDE:DO_VIEW+PORTAL_MACROS);
    button(s,paused?retract:"EXCLUIR",paused?"retract":"exclude",580,168,208,144,CYAN,paused?DO_RETRACT:DO_VIEW+PORTAL_EXCLUDE);
    button(s,"AJUSTES DE IMPRESIÓN","settings",360,324,208,144,CYAN,DO_TUNING);
    button(s,"PARADA","stop",580,324,208,144,RED,DO_EMERGENCY);
}
static void dropdown_style(lv_obj_t *object)
{
    style_surface(object,BUTTON,BORDER,6);lv_obj_set_style_text_font(object,&lv_font_montserrat_16,0);
    lv_obj_set_style_text_color(object,lv_color_hex(WHITE),0);lv_obj_set_style_pad_all(object,10,0);
    lv_obj_t *list=lv_dropdown_get_list(object);if(list){style_surface(list,BUTTON,CYAN,6);lv_obj_set_style_text_font(list,&lv_font_montserrat_20,0);lv_obj_set_style_text_color(list,lv_color_hex(WHITE),0);}
}
static void dropdown_event(lv_event_t *event)
{
    if(settings_input_blocked())return;
    if(lv_event_get_code(event)!=LV_EVENT_VALUE_CHANGED&&lv_event_get_code(event)!=LV_EVENT_READY)return;
    lv_obj_t *obj=lv_event_get_target(event);dropdown_style(obj);
    if(lv_event_get_code(event)==LV_EVENT_VALUE_CHANGED){if(obj==s_material)s_material_index=lv_dropdown_get_selected(obj);else s_step_index=lv_dropdown_get_selected(obj);}
}
static void create_toolhead(void)
{
    lv_obj_t *s=s_screens[PORTAL_TOOLHEAD];
    for(unsigned i=0;i<2;++i){int y=12+i*146;dark_card(s,12,y,296,134);label(s,i?"Cama":"Boquilla",73,y+13,160,&lv_font_montserrat_20,WHITE);
        icon(s,i?"bed":"nozzle",29,y+8,32,ORANGE);
        s_tool_current[i]=label(s,"-- °C",30,y+56,150,&lv_font_montserrat_28,WHITE);
        label(s,"OBJETIVO",192,y+38,105,&lv_font_montserrat_14,MUTED);
        lv_obj_t *b=button(s,"0 °C",NULL,186,y+68,104,40,ORANGE,i?DO_EDIT_BED:DO_EDIT_HOTEND);s_tool_target[i]=lv_obj_get_child(b,0);
    }
    s_material=lv_dropdown_create(s);lv_dropdown_set_options(s_material,"PLA\nPETG\nTPU\nABS");lv_obj_set_pos(s_material,12,304);lv_obj_set_size(s_material,143,44);dropdown_style(s_material);lv_obj_add_event_cb(s_material,dropdown_event,LV_EVENT_READY,NULL);lv_obj_add_event_cb(s_material,dropdown_event,LV_EVENT_VALUE_CHANGED,NULL);
    button(s,"APLICAR",NULL,165,304,143,44,GREEN,DO_MATERIAL_APPLY);button(s,"ENFRIAR","cool",12,424,296,44,CYAN,DO_COOL);
    large_panel(s,320,12,468,456);label(s,"Movimiento",338,26,220,&lv_font_montserrat_16,PORTAL_TEXT_COLOR);
    label(s,"PASO",629,119,52,&lv_font_montserrat_14,PORTAL_TEXT_COLOR);
    s_step=lv_dropdown_create(s);lv_dropdown_set_options(s_step,"1 mm\n5 mm\n10 mm\n20 mm\n50 mm\n100 mm");lv_dropdown_set_selected(s_step,2);lv_obj_set_pos(s_step,681,107);lv_obj_set_size(s_step,100,44);dropdown_style(s_step);lv_obj_add_event_cb(s_step,dropdown_event,LV_EVENT_READY,NULL);lv_obj_add_event_cb(s_step,dropdown_event,LV_EVENT_VALUE_CHANGED,NULL);
    static const char *const captions[][4]={{"X -","X","X +","X/ --"},{"Y -","Y","Y +","Y/ --"},{"Z -","Z","Z +","Z/ --"}};
    static const int position_x[]={362,447,531},control_y[]={162,232,303};
    for(unsigned axis=0;axis<3;++axis){
        lv_obj_t *position=panel(s,position_x[axis],69,76,40);
        s_axis_position[axis]=center_label(position,captions[axis][3],2,10,70,&lv_font_montserrat_16,WHITE);
        int y=control_y[axis];
        button(s,captions[axis][0],"left",358,y,76,67,CYAN,DO_X_MINUS+axis*2);
        button(s,captions[axis][1],"home",443,y,76,67,GREEN,DO_HOME_X+axis);
        button(s,captions[axis][2],"right",528,y,76,67,CYAN,DO_X_PLUS+axis*2);
    }
    button(s,"HOME TODOS","home",335,394,209,44,GREEN,DO_HOME_ALL);
    button(s,"Desactivar motores","motors_off",556,394,215,44,CYAN,DO_MOTORS_OFF);
}
static void create_files(void)
{
    lv_obj_t *s=s_screens[PORTAL_FILES];large_panel(s,12,12,358,456);label(s,"ARCHIVOS RECIENTES",26,24,320,&lv_font_montserrat_14,PORTAL_TEXT_COLOR);
    s_files_list=colored_panel(s,22,50,338,406,PORTAL_MEDIUM_COLOR);lv_obj_add_flag(s_files_list,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(s_files_list,LV_DIR_VER);
    large_panel(s,382,12,406,456);s_file_title=label(s,"Selecciona un archivo",396,24,378,&lv_font_montserrat_20,PORTAL_TEXT_COLOR);lv_label_set_long_mode(s_file_title,LV_LABEL_LONG_SCROLL_CIRCULAR);
    create_preview(&s_previews[2],s,396,80,300,300);
    s_file_tools=large_panel(s,716,58,58,338);lv_obj_add_flag(s_file_tools,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(s_file_tools,LV_DIR_VER);
    s_file_print=button(s,"IMPRIMIR",NULL,396,412,378,48,GREEN,DO_PRINT);
}
static void create_macros(void)
{
    lv_obj_t *s=s_screens[PORTAL_MACROS];large_panel(s,12,12,468,456);label(s,"MACROS DE LA IMPRESORA",32,28,420,&lv_font_montserrat_14,PORTAL_TEXT_COLOR);
    s_macros_list=colored_panel(s,25,58,442,394,PORTAL_MEDIUM_COLOR);lv_obj_add_flag(s_macros_list,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(s_macros_list,LV_DIR_VER);
    large_panel(s,496,12,292,456);
    icon(s,"macro",610,57,64,PORTAL_BLUE);
    center_label(s,"Macro seleccionada",513,159,258,&lv_font_montserrat_16,PORTAL_TEXT_COLOR);
    s_macro_name=center_label(s,"---",513,195,258,&lv_font_montserrat_16,PORTAL_TEXT_COLOR);lv_label_set_long_mode(s_macro_name,LV_LABEL_LONG_WRAP);
    center_label(s,"Sin parámetros",513,280,258,&lv_font_montserrat_16,PORTAL_TEXT_COLOR);
    s_macro_run=button(s,"EJECUTAR","play",513,352,258,98,YELLOW,DO_MACRO);
}
static void create_exclude(void)
{
    lv_obj_t *s=s_screens[PORTAL_EXCLUDE];large_panel(s,12,12,450,456);
    s_map_info=label(s,"LÍMITES DE CAMA: ---",33,28,410,&lv_font_montserrat_14,PORTAL_TEXT_COLOR);
    s_map=colored_panel(s,86,82,312,312,PORTAL_MEDIUM_COLOR);lv_obj_set_style_radius(s_map,0,0);
    label(s,"Y",64,376,30,&lv_font_montserrat_14,PORTAL_TEXT_COLOR);label(s,"X",412,376,30,&lv_font_montserrat_14,PORTAL_TEXT_COLOR);
    large_panel(s,478,12,310,456);label(s,"OBJETOS",496,28,265,&lv_font_montserrat_14,PORTAL_TEXT_COLOR);
    s_objects_list=large_panel(s,491,58,284,300);lv_obj_add_flag(s_objects_list,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(s_objects_list,LV_DIR_VER);
    label(s,"Selecciona en el mapa o la lista.",496,364,275,&lv_font_montserrat_14,PORTAL_TEXT_COLOR);
    s_exclude_run=button(s,"EXCLUIR SELECCIÓN","exclude",493,391,280,72,CYAN,DO_EXCLUDE);
}
static void slider_reset(lv_event_t *event)
{
    /* LVGL resets do not finish the slider's internal drag on their own. */
    lv_obj_send_event(lv_event_get_target(event),LV_EVENT_PRESS_LOST,NULL);
}
static void brightness_event(lv_event_t *event)
{
    if(lv_event_get_code(event)==LV_EVENT_PRESS_LOST){
        s_adjusting_brightness=false;
        /* Cancel the live preview; an interrupted contact must not persist it. */
        s_brightness=s_brightness_before_adjustment;
        lv_slider_set_value(s_brightness_slider,s_brightness,LV_ANIM_OFF);
        apply_brightness(s_brightness);format(s_brightness_text,"%u %%",s_brightness);return;
    }
    if(settings_input_blocked())return;
    if(lv_event_get_code(event)==LV_EVENT_PRESSED){s_brightness_before_adjustment=s_brightness;s_adjusting_brightness=true;return;}
    unsigned value=lv_slider_get_value(lv_event_get_target(event));s_brightness=value;
    apply_brightness(value);format(s_brightness_text,"%u %%",value);
    if(lv_event_get_code(event)==LV_EVENT_RELEASED){
        s_adjusting_brightness=false;
        if(device_network_set_display(s_brightness,s_sleep_minutes)!=ESP_OK)toast("No se pudo guardar el brillo");
    }
}
static void timeout_event(lv_event_t *event)
{
    if(settings_input_blocked())return;
    static const unsigned minutes[]={1,5,10};s_sleep_minutes=minutes[lv_dropdown_get_selected(s_timeout)%3];
    if(device_network_set_display(s_brightness,s_sleep_minutes)!=ESP_OK)toast("No se pudo guardar el reposo");
}
static void audio_enabled_event(lv_event_t *event)
{
    if(settings_input_blocked())return;
    bool enabled=lv_obj_has_state(lv_event_get_target(event),LV_STATE_CHECKED);
    if(audio_service_set_enabled(enabled)!=ESP_OK)toast("No se pudo guardar el sonido");
}
static void audio_volume_event(lv_event_t *event)
{
    if(lv_event_get_code(event)==LV_EVENT_PRESS_LOST){
        s_adjusting_volume=false;
        audio_service_status_t audio={0};audio_service_get_status(&audio);
        lv_slider_set_value(s_volume_slider,audio.volume,LV_ANIM_OFF);format(s_volume_text,"%u %%",audio.volume);return;
    }
    if(settings_input_blocked())return;
    if(lv_event_get_code(event)==LV_EVENT_PRESSED){s_adjusting_volume=true;return;}
    unsigned value=lv_slider_get_value(lv_event_get_target(event));
    format(s_volume_text,"%u %%",value);
    if(lv_event_get_code(event)==LV_EVENT_RELEASED){
        s_adjusting_volume=false;
        if(audio_service_set_volume(value)!=ESP_OK)toast("No se pudo guardar el volumen");
    }
}
static void update_audio_settings(void)
{
    audio_service_status_t audio={0};audio_service_get_status(&audio);
    const lv_image_dsc_t *source=portal_icon_for_symbol(audio.enabled&&audio.volume?"audio":"mute");
    if(s_audio_state_icon&&lv_image_get_src(s_audio_state_icon)!=source)lv_image_set_src(s_audio_state_icon,source);
    if(audio.enabled)lv_obj_add_state(s_audio_switch,LV_STATE_CHECKED);
    else lv_obj_remove_state(s_audio_switch,LV_STATE_CHECKED);
    if(!s_adjusting_volume){lv_slider_set_value(s_volume_slider,audio.volume,LV_ANIM_OFF);format(s_volume_text,"%u %%",audio.volume);}
    if(audio.ready&&audio.enabled&&audio.volume)lv_obj_remove_state(s_audio_test,LV_STATE_DISABLED);
    else lv_obj_add_state(s_audio_test,LV_STATE_DISABLED);
    set_text(s_audio_status,!audio.enabled?"Silenciado; avisos vaciados":!audio.ready?"Audio no disponible; revisa consola":
        audio.last_error!=ESP_OK?"Error de ajustes; revisa consola":audio.playing?"Reproduciendo audio...":
        audio.settings_pending?"Guardando ajustes...":audio.volume?"Listo: sonidos de Portal":"Volumen a cero");
}
static void create_settings(void)
{
    lv_obj_t *s=s_screens[PORTAL_SETTINGS];colored_panel(s,12,12,776,456,PORTAL_STATUS_COLOR);
    label(s,"Brillo de pantalla",39,36,350,&lv_font_montserrat_20,WHITE);
    s_brightness_text=label(s,"80 %",350,84,80,&lv_font_montserrat_20,PORTAL_PALE_ORANGE);
    s_brightness_slider=lv_slider_create(s);lv_slider_set_range(s_brightness_slider,5,100);lv_slider_set_value(s_brightness_slider,80,LV_ANIM_OFF);lv_obj_set_pos(s_brightness_slider,39,96);lv_obj_set_size(s_brightness_slider,278,8);
    lv_obj_set_ext_click_area(s_brightness_slider,20);
    lv_obj_remove_flag(s_brightness_slider,LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_bg_color(s_brightness_slider,lv_color_hex(ORANGE),LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_brightness_slider,lv_color_hex(WHITE),LV_PART_KNOB);
    lv_obj_add_event_cb(s_brightness_slider,brightness_event,LV_EVENT_VALUE_CHANGED,NULL);lv_obj_add_event_cb(s_brightness_slider,brightness_event,LV_EVENT_RELEASED,NULL);
    lv_obj_add_event_cb(s_brightness_slider,brightness_event,LV_EVENT_PRESSED,NULL);
    lv_obj_add_event_cb(s_brightness_slider,brightness_event,LV_EVENT_PRESS_LOST,NULL);
    lv_obj_add_event_cb(s_brightness_slider,slider_reset,LV_EVENT_INDEV_RESET,NULL);
    label(s,"Activar salvapantallas tras",39,156,400,&lv_font_montserrat_20,WHITE);
    s_timeout=lv_dropdown_create(s);lv_dropdown_set_options(s_timeout,"1 MIN\n5 MIN\n10 MIN");lv_dropdown_set_selected(s_timeout,2);lv_obj_set_pos(s_timeout,251,198);lv_obj_set_size(s_timeout,178,44);dropdown_style(s_timeout);lv_obj_add_event_cb(s_timeout,timeout_event,LV_EVENT_VALUE_CHANGED,NULL);
    label(s,"TEMA",39,279,200,&lv_font_montserrat_14,WHITE);label(s,"PORTAL",318,276,110,&lv_font_montserrat_20,PORTAL_PALE_BLUE);
    s_audio_state_icon=icon(s,"audio",477,30,32,PORTAL_BLUE);
    label(s,"Sonido",521,36,136,&lv_font_montserrat_20,WHITE);
    s_audio_switch=lv_switch_create(s);lv_obj_set_pos(s_audio_switch,690,30);lv_obj_set_size(s_audio_switch,62,34);
    lv_obj_set_ext_click_area(s_audio_switch,8);
    lv_obj_set_style_bg_color(s_audio_switch,lv_color_hex(CYAN),LV_PART_INDICATOR|LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_audio_switch,audio_enabled_event,LV_EVENT_VALUE_CHANGED,NULL);
    label(s,"Volumen",477,104,180,&lv_font_montserrat_20,WHITE);
    s_volume_text=label(s,"50 %",690,104,72,&lv_font_montserrat_20,PORTAL_PALE_ORANGE);
    s_volume_slider=lv_slider_create(s);lv_slider_set_range(s_volume_slider,0,100);lv_slider_set_value(s_volume_slider,50,LV_ANIM_OFF);lv_obj_set_pos(s_volume_slider,477,154);lv_obj_set_size(s_volume_slider,275,8);
    lv_obj_set_ext_click_area(s_volume_slider,20);
    lv_obj_remove_flag(s_volume_slider,LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_bg_color(s_volume_slider,lv_color_hex(CYAN),LV_PART_INDICATOR);lv_obj_set_style_bg_color(s_volume_slider,lv_color_hex(WHITE),LV_PART_KNOB);
    lv_obj_add_event_cb(s_volume_slider,audio_volume_event,LV_EVENT_PRESSED,NULL);lv_obj_add_event_cb(s_volume_slider,audio_volume_event,LV_EVENT_VALUE_CHANGED,NULL);lv_obj_add_event_cb(s_volume_slider,audio_volume_event,LV_EVENT_RELEASED,NULL);
    lv_obj_add_event_cb(s_volume_slider,audio_volume_event,LV_EVENT_PRESS_LOST,NULL);
    lv_obj_add_event_cb(s_volume_slider,slider_reset,LV_EVENT_INDEV_RESET,NULL);
    s_audio_test=button(s,"PROBAR AUDIO","audio",477,200,275,55,CYAN,DO_AUDIO_TEST);
    s_audio_status=label(s,"Inicializando audio...",477,274,275,&lv_font_montserrat_14,WHITE);
    label(s,"CONFIGURACIÓN DE RED",39,336,700,&lv_font_montserrat_14,WHITE);
    button(s,"WI-FI Y MOONRAKER","wifi",39,366,405,54,CYAN,DO_NETWORK);
    s_network_text=label(s,"",462,370,296,&lv_font_montserrat_14,WHITE);
}
static void create_error(void)
{
    lv_obj_t *s=s_screens[PORTAL_ERROR];
    artwork(s,&portal_core_error,39,10);
    large_panel(s,12,342,264,126);
    center_label(s,"Klipper requiere atención",26,360,236,&lv_font_montserrat_14,PORTAL_TEXT_COLOR);
    lv_obj_t *message=large_panel(s,308,12,480,278);lv_obj_add_flag(message,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(message,LV_DIR_VER);
    label(message,"ERROR DE KLIPPER",5,18,463,&lv_font_montserrat_20,PORTAL_RED);
    s_error_text=label(message,"---",5,55,463,&lv_font_montserrat_20,PORTAL_TEXT_COLOR);lv_label_set_long_mode(s_error_text,LV_LABEL_LONG_WRAP);
    button(s,"REINICIAR KLIPPER","refresh",292,306,242,162,CYAN,DO_RESTART);
    button(s,"APAGAR EQUIPO\nANFITRIÓN","power",546,306,242,162,RED,DO_SHUTDOWN);
    button(s,"AJUSTES","settings",28,398,232,56,CYAN,DO_VIEW+PORTAL_SETTINGS);
}
static void create_details(void)
{
    lv_obj_t *s=s_screens[PORTAL_DETAILS];large_panel(s,12,12,336,456);create_preview(&s_previews[3],s,30,40,300,300);
    s_details_file=center_label(s,"---",26,408,308,&lv_font_montserrat_16,PORTAL_TEXT_COLOR);lv_label_set_long_mode(s_details_file,LV_LABEL_LONG_SCROLL_CIRCULAR);
    large_panel(s,360,12,428,456);
    s_details_result=label(s,"SIN TRABAJOS ANTERIORES",378,27,392,&lv_font_montserrat_20,PORTAL_BLUE);
    label(s,"TIEMPO ESTIMADO",378,78,392,&lv_font_montserrat_14,PORTAL_TEXT_COLOR);s_details_time=label(s,"---",378,101,392,&lv_font_montserrat_28,PORTAL_TEXT_COLOR);
    label(s,"PESO / LONGITUD",378,157,392,&lv_font_montserrat_14,PORTAL_TEXT_COLOR);s_details_weight=label(s,"---",378,180,392,&lv_font_montserrat_20,PORTAL_TEXT_COLOR);
    label(s,"FILAMENTOS ACTIVOS",378,238,392,&lv_font_montserrat_14,PORTAL_TEXT_COLOR);s_details_colors=label(s,"---",378,261,392,&lv_font_montserrat_16,PORTAL_TEXT_COLOR);
    s_details_reset=button(s,"LIMPIAR ESTADO Y VOLVER A ESPERA",NULL,378,353,392,52,CYAN,DO_RESET_JOB);
    lv_obj_set_style_text_font(lv_obj_get_child(s_details_reset,0),&lv_font_montserrat_16,0);lv_obj_center(lv_obj_get_child(s_details_reset,0));
    button(s,"VOLVER",NULL,378,414,392,42,CYAN,DO_BACK);
}
static void close_modal(void)
{
    if(s_modal){lv_obj_delete(s_modal);s_modal=NULL;}s_modal_action=0;s_target_text=NULL;
    memset(s_tuning_actual,0,sizeof(s_tuning_actual));
}
static bool tuning_value(unsigned row,float *value)
{
    if(row==0){*value=s_printer.speed_percent;return s_printer.speed_factor_valid&&isfinite(*value)&&*value>=0;}
    if(row==1){*value=s_printer.flow_percent;return s_printer.flow_factor_valid&&isfinite(*value)&&*value>=0;}
    *value=s_printer.fan_percent;return s_printer.fan_speed_valid&&isfinite(*value)&&*value>=0&&*value<=100;
}
static bool tuning_context_valid(void)
{
    if(s_modal_action!=DO_TUNING||!s_modal||portal_saver_active()||inspection_input_blocked()||
        !s_printer.available||!s_printer.socket_connected||!s_printer.initial_snapshot||
        s_printer.endpoint_generation!=s_modal_generation||s_network.endpoint_generation!=s_modal_generation||
        s_printer.job_id!=s_tuning_job||strcmp(s_printer.filename,s_modal_file)||
        s_printer.state!=s_tuning_state||s_printer.raw_state!=s_tuning_raw_state||s_view!=s_tuning_view)return false;
    for(unsigned row=0;row<3;++row){float value;if(tuning_value(row,&value)!=s_tuning_valid[row])return false;}
    return true;
}
static void tuning_update(void)
{
    if(s_modal_action!=DO_TUNING)return;
    if(!tuning_context_valid()){close_modal();toast("Ajustes cerrados: cambio el estado de la impresora");return;}
    for(unsigned row=0;row<3;++row){
        float value;bool valid=tuning_value(row,&value);
        if(valid){format(s_tuning_actual[row],"%.0f %%",value);format(s_tuning_proposed[row],"%u %%",s_tuning_draft[row]);}
        else{set_text(s_tuning_actual[row],"-- %");set_text(s_tuning_proposed[row],"-- %");}
        enabled(s_tuning_minus[row],valid&&!s_tuning_pending[row]&&s_tuning_draft[row]>TUNING_MIN[row]);
        enabled(s_tuning_plus[row],valid&&!s_tuning_pending[row]&&s_tuning_draft[row]<TUNING_MAX[row]);
        enabled(s_tuning_apply[row],valid&&!s_tuning_pending[row]);
    }
}
static void tuning_event(lv_event_t *event)
{
    if(portal_saver_active()||inspection_input_blocked()||s_modal_action!=DO_TUNING)return;
    unsigned action=(uintptr_t)lv_event_get_user_data(event);
    if(action==9){close_modal();return;}
    // Re-read both owners immediately before changing a draft or submitting it.
    device_network_snapshot(&s_network);klipper_client_snapshot(&s_printer);
    if(!tuning_context_valid()){close_modal();toast("El trabajo o la conexion cambio; abre Ajustes de nuevo");return;}
    unsigned row=action/3,operation=action%3;
    if(row>=3||!s_tuning_valid[row]||s_tuning_pending[row])return;
    unsigned value=s_tuning_draft[row];
    if(operation==0)s_tuning_draft[row]=value>TUNING_MIN[row]+5?value-5:TUNING_MIN[row];
    else if(operation==1)s_tuning_draft[row]=value+5<TUNING_MAX[row]?value+5:TUNING_MAX[row];
    else{
        static const klipper_tuning_t controls[]={K_TUNING_SPEED,K_TUNING_FLOW,K_TUNING_FAN};
        if(klipper_client_set_print_tuning_with_context(controls[row],value,s_modal_generation,s_tuning_job,AUDIO_CLIP_TUNING)){
            s_tuning_pending[row]=true;toast("Ajuste en espera");
        }else toast("No se pudo encolar el ajuste; revisa la impresora");
    }
    tuning_update();
}
static void tuning_open(void)
{
    if(portal_saver_active()||inspection_input_blocked()||s_print_pending||
        (s_view!=PORTAL_PRINTING&&s_view!=PORTAL_PAUSED)||!s_printer.available||
        !s_printer.socket_connected||!s_printer.initial_snapshot||
        s_network.endpoint_generation!=s_printer.endpoint_generation||
        (s_printer.raw_state!=KLIPPER_PRINTING&&s_printer.raw_state!=KLIPPER_PAUSED)||
        (s_printer.state!=KLIPPER_PRINTING&&s_printer.state!=KLIPPER_PAUSED)){
        toast("Ajustes disponibles durante impresion o pausa");return;
    }
    portal_network_settings_close();close_modal();s_modal_action=DO_TUNING;
    s_modal_generation=s_printer.endpoint_generation;s_tuning_job=s_printer.job_id;
    s_tuning_state=s_printer.state;s_tuning_raw_state=s_printer.raw_state;s_tuning_view=s_view;
    snprintf(s_modal_file,sizeof(s_modal_file),"%s",s_printer.filename);
    s_modal=lv_obj_create(lv_layer_top());lv_obj_remove_style_all(s_modal);lv_obj_set_size(s_modal,800,480);
    lv_obj_set_style_bg_color(s_modal,lv_color_hex(PORTAL_BACKGROUND_COLOR),0);lv_obj_set_style_bg_opa(s_modal,LV_OPA_COVER,0);
    portal_background(s_modal);
    lv_obj_add_flag(s_modal,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_modal,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_EVENT_BUBBLE|LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_t *p=large_panel(s_modal,20,12,760,456);
    center_label(p,"AJUSTES DE IMPRESIÓN",16,12,728,&lv_font_montserrat_20,PORTAL_BLUE);
    center_label(p,"Cada APLICAR envía solo su fila. Paso: 5 %.",16,40,728,&lv_font_montserrat_16,PORTAL_TEXT_COLOR);
    static const char *names[]={"VELOCIDAD","FLUJO","VENTILADOR CAPA"};
    for(unsigned row=0;row<3;++row){
        float value;s_tuning_valid[row]=tuning_value(row,&value);s_tuning_pending[row]=false;
        // The proposed value is a bounded draft; the actual reading stays separate.
        float bounded=fminf((float)TUNING_MAX[row],fmaxf((float)TUNING_MIN[row],value));
        s_tuning_draft[row]=s_tuning_valid[row]?(unsigned)lroundf(bounded):TUNING_MIN[row];
        lv_obj_t *r=colored_panel(p,12,70+row*100,736,92,PORTAL_STATUS_COLOR);
        label(r,names[row],12,10,150,row==2?&lv_font_montserrat_14:&lv_font_montserrat_16,WHITE);
        format(label(r,"",12,40,150,&lv_font_montserrat_14,WHITE),"%u - %u %%",TUNING_MIN[row],TUNING_MAX[row]);
        center_label(r,"ACTUAL",165,8,104,&lv_font_montserrat_14,WHITE);
        s_tuning_actual[row]=center_label(r,"-- %",165,36,104,&lv_font_montserrat_20,WHITE);
        center_label(r,"PROPUESTO",346,8,116,&lv_font_montserrat_14,WHITE);
        s_tuning_proposed[row]=center_label(r,"-- %",346,38,116,&lv_font_montserrat_20,PORTAL_PALE_ORANGE);
        s_tuning_minus[row]=button(r,"","minus",282,27,54,54,CYAN,0);
        s_tuning_plus[row]=button(r,"","plus",472,27,54,54,CYAN,0);
        s_tuning_apply[row]=button(r,"APLICAR",NULL,550,27,172,54,GREEN,0);
        lv_obj_t *buttons[]={s_tuning_minus[row],s_tuning_plus[row],s_tuning_apply[row]};
        for(unsigned op=0;op<3;++op){lv_obj_remove_event_cb(buttons[op],click);lv_obj_add_event_cb(buttons[op],tuning_event,LV_EVENT_CLICKED,(void*)(uintptr_t)(row*3+op));}
    }
    lv_obj_t *back=button(p,"CERRAR",NULL,250,383,260,52,CYAN,0);
    lv_obj_remove_event_cb(back,click);lv_obj_add_event_cb(back,tuning_event,LV_EVENT_CLICKED,(void*)(uintptr_t)9);
    tuning_update();
}
static void modal_button(lv_event_t *event);
static void modal_open(unsigned action,const char *title,const char *message,bool danger)
{
    portal_network_settings_close();close_modal();s_modal_action=action;
    s_modal_generation=s_printer.endpoint_generation;
    snprintf(s_modal_file,sizeof(s_modal_file),"%s",action==DO_PRINT?s_selected_file:s_printer.filename);
    s_modal=lv_obj_create(lv_layer_top());lv_obj_remove_style_all(s_modal);lv_obj_set_size(s_modal,800,480);
    lv_obj_set_style_bg_color(s_modal,lv_color_hex(PORTAL_BACKGROUND_COLOR),0);lv_obj_set_style_bg_opa(s_modal,LV_OPA_COVER,0);
    portal_background(s_modal);
    lv_obj_remove_flag(s_modal,LV_OBJ_FLAG_SCROLLABLE);
    bool warning=action==DO_CANCEL||action==DO_EMERGENCY||action==DO_SHUTDOWN;
    uint32_t fill=warning?PORTAL_STATUS_COLOR:PORTAL_PANEL_COLOR;
    lv_obj_t *p=colored_panel(s_modal,140,70,520,340,fill);
    lv_obj_set_style_border_color(p,lv_color_hex(danger?RED:CYAN),0);
    if(warning)artwork(p,&portal_core_warning,14,6);
    int body_x=warning?230:20,body_width=500-body_x;
    int body_y=action==DO_CANCEL?117:action==DO_SHUTDOWN?87:warning?102:70;
    center_label(p,title,body_x,18,body_width,button_font(title,body_width,false),warning?PORTAL_PALE_RED:danger?RED:CYAN);
    lv_obj_t *body=colored_panel(p,body_x,body_y,body_width,warning?146:180,fill);
    lv_obj_set_style_border_width(body,0,0);lv_obj_add_flag(body,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(body,LV_DIR_VER);
    lv_obj_t *copy=label(body,message,5,0,body_width-14,
                        warning?&lv_font_montserrat_16:&lv_font_montserrat_20,warning?WHITE:PORTAL_TEXT_COLOR);
    if(warning)lv_obj_set_style_text_align(copy,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_t *back=button(p,"VOLVER",NULL,20,275,210,46,CYAN,0);lv_obj_remove_event_cb(back,click);lv_obj_add_event_cb(back,modal_button,LV_EVENT_CLICKED,(void*)0);
    lv_obj_t *apply=button(p,action==DO_SHUTDOWN?"APAGAR":action?"CONFIRMAR":"CERRAR",NULL,242,275,258,46,danger?RED:GREEN,0);lv_obj_remove_event_cb(apply,click);lv_obj_add_event_cb(apply,modal_button,LV_EVENT_CLICKED,(void*)1);
}
static bool queue_script_at(const char *script,uint32_t generation,uint32_t context)
{
    if(inspection_input_blocked())return false;
    if(!s_printer.available&&strcmp(script,"FIRMWARE_RESTART")){toast("Klipper no esta listo para recibir ordenes");return false;}
    if(!klipper_client_send_gcode_at(script,generation,context)){toast("No se pudo encolar la orden");return false;}
    return true;
}
static bool queue_script(const char *script,uint32_t context){return queue_script_at(script,s_printer.endpoint_generation,context);}
static bool start_print(const char *filename,uint32_t generation)
{
    if(inspection_input_blocked()||s_print_pending||!s_printer.available||
        s_printer.raw_state==KLIPPER_PRINTING||s_printer.raw_state==KLIPPER_PAUSED||
        !filename||!*filename)return false;
    if(!klipper_client_action(K_ACTION_PRINT_FILE,filename,generation))return false;
    s_print_pending_job=s_printer.job_id;snprintf(s_print_pending_file,sizeof(s_print_pending_file),"%s",filename);
    s_print_pending=true;s_print_uncertain=false;enabled(s_file_print,false);
    // show_view owns dismissal; returning callers must not delete a new result modal.
    show_view(live_view());return true;
}
static void modal_button(lv_event_t *event)
{
    if(inspection_input_blocked())return;
    bool confirm=(uintptr_t)lv_event_get_user_data(event)!=0;unsigned action=s_modal_action;
    if(!confirm){close_modal();return;}
    device_network_snapshot_t current_network;device_network_snapshot(&current_network);
    if(action && current_network.endpoint_generation!=s_modal_generation){close_modal();toast("Destino cambiado; confirma de nuevo");return;}
    klipper_client_snapshot(&s_printer);
    if(action==DO_PRINT&&(!s_printer.available||s_printer.state==KLIPPER_PRINTING||s_printer.state==KLIPPER_PAUSED)){
        close_modal();toast("La impresora ya no esta lista para iniciar");return;
    }
    if(action==DO_CANCEL&&(strcmp(s_modal_file,s_printer.filename)||
        (s_printer.state!=KLIPPER_PRINTING&&s_printer.state!=KLIPPER_PAUSED))){
        close_modal();toast("El trabajo cambio; confirma de nuevo");return;
    }
    if(action==DO_EDIT_HOTEND||action==DO_EDIT_BED){
        unsigned value=(unsigned)strtoul(s_target_digits,NULL,10),limit=s_target_bed?120:300;
        if(value>limit){toast("Objetivo fuera de rango");return;}
        char gcode[64];snprintf(gcode,sizeof(gcode),s_target_bed?"M140 S%u":"M104 S%u",value);queue_script_at(gcode,s_modal_generation,AUDIO_CLIP_TEMPERATURE);
    }else if(action==DO_EMERGENCY){
        if(klipper_client_action(K_ACTION_EMERGENCY,NULL,s_modal_generation))audio_service_play(AUDIO_CLIP_SYSTEM);
        else toast("No se pudo solicitar la parada");
    }
    else if(action==DO_CANCEL)queue_script_at("CANCEL_PRINT",s_modal_generation,0);
    else if(action==DO_PRINT){if(!start_print(s_modal_file,s_modal_generation))toast("No se pudo iniciar la impresion");return;}
    else if(action==DO_RESTART){
        if(queue_script_at("FIRMWARE_RESTART",s_modal_generation,0))audio_service_play(AUDIO_CLIP_SYSTEM);
    }
    else if(action==DO_SHUTDOWN){
        if(klipper_client_action(K_ACTION_SHUTDOWN,NULL,s_modal_generation))audio_service_play(AUDIO_CLIP_SYSTEM);
        else toast("No se pudo solicitar el apagado");
    }
    close_modal();
}
static void digit_event(lv_event_t *event)
{
    if(inspection_input_blocked())return;
    unsigned value=(uintptr_t)lv_event_get_user_data(event);size_t length=strlen(s_target_digits);
    if(value==10){if(length)s_target_digits[length-1]=0;}
    else if(value==11)s_target_digits[0]=0;
    else if(length<3){if(length==1&&s_target_digits[0]=='0')length=0;s_target_digits[length]=(char)('0'+value);s_target_digits[length+1]=0;}
    format(s_target_text,"%s °C",s_target_digits[0]?s_target_digits:"0");
}
static void edit_target(bool bed)
{
    close_modal();s_target_bed=bed;s_modal_action=bed?DO_EDIT_BED:DO_EDIT_HOTEND;s_modal_generation=s_printer.endpoint_generation;
    snprintf(s_target_digits,sizeof(s_target_digits),"%.0f",bed?s_printer.bed_target:s_printer.hotend_target);
    s_modal=lv_obj_create(lv_layer_top());lv_obj_remove_style_all(s_modal);lv_obj_set_size(s_modal,800,480);lv_obj_set_style_bg_color(s_modal,lv_color_hex(PORTAL_BACKGROUND_COLOR),0);lv_obj_set_style_bg_opa(s_modal,LV_OPA_COVER,0);lv_obj_remove_flag(s_modal,LV_OBJ_FLAG_SCROLLABLE);
    portal_background(s_modal);
    lv_obj_t *p=large_panel(s_modal,140,40,520,400);center_label(p,bed?"OBJETIVO - CAMA":"OBJETIVO - BOQUILLA",15,16,490,&lv_font_montserrat_20,PORTAL_BLUE);
    /* The keypad surface belongs to the control, not to the dialog panel. */
    lv_obj_t *keypad=panel(p,104,114,306,210);lv_obj_set_style_border_width(keypad,0,0);
    s_target_text=center_label(p,"",100,58,320,&lv_font_montserrat_48,ORANGE);format(s_target_text,"%s °C",s_target_digits);
    const unsigned values[]={1,2,3,4,5,6,7,8,9,10,0,11};
    for(unsigned i=0;i<12;++i){char name[16];unsigned v=values[i];if(v==10)snprintf(name,sizeof(name),LV_SYMBOL_BACKSPACE);else if(v==11)snprintf(name,sizeof(name),"BORRAR");else snprintf(name,sizeof(name),"%u",v);
        lv_obj_t *b=button(p,v==10?"":name,v==10?"backspace":NULL,110+(i%3)*102,120+(i/3)*51,94,44,CYAN,0);
        if(v==11){lv_obj_t *caption=lv_obj_get_child(b,0);lv_obj_set_style_text_font(caption,&lv_font_montserrat_16,0);lv_obj_center(caption);}
        lv_obj_remove_event_cb(b,click);lv_obj_add_event_cb(b,digit_event,LV_EVENT_CLICKED,(void*)(uintptr_t)v);
    }
    lv_obj_t *back=button(p,"VOLVER",NULL,20,346,210,46,CYAN,0);lv_obj_remove_event_cb(back,click);lv_obj_add_event_cb(back,modal_button,LV_EVENT_CLICKED,(void*)0);
    lv_obj_t *apply=button(p,"APLICAR",NULL,242,346,258,46,GREEN,0);lv_obj_remove_event_cb(apply,click);lv_obj_add_event_cb(apply,modal_button,LV_EVENT_CLICKED,(void*)1);
}
static void jog(unsigned action)
{
    char axis='X';int sign=1;
    if(action==DO_X_MINUS)sign=-1;else if(action==DO_Y_MINUS){axis='Y';sign=-1;}else if(action==DO_Y_PLUS)axis='Y';
    else if(action==DO_Z_MINUS){axis='Z';sign=-1;}else if(action==DO_Z_PLUS)axis='Z';
    char script[256];snprintf(script,sizeof(script),"SAVE_GCODE_STATE NAME=JC_PANEL_MOVE\nG91\nG1 %c%d F%u\nRESTORE_GCODE_STATE NAME=JC_PANEL_MOVE MOVE=0",axis,sign*(int)STEPS[s_step_index],axis=='Z'?600:6000);queue_script(script,AUDIO_CLIP_MOVEMENT);
}
static void click(lv_event_t *event)
{
    if(portal_saver_active()||s_modal||inspection_input_blocked())return;
    unsigned action=(uintptr_t)lv_event_get_user_data(event);
    if(action>=DO_OBJECT){unsigned i=action-DO_OBJECT;if(i<s_printer.objects_count){snprintf(s_selected_object,sizeof(s_selected_object),"%s",s_printer.objects[i].name);s_objects_count=~0U;update_all();}return;}
    if(action>=DO_MACRO_SELECT){unsigned i=action-DO_MACRO_SELECT;if(i<s_printer.macros_count&&s_printer.macros[i][0]!='_'){snprintf(s_selected_macro,sizeof(s_selected_macro),"%s",s_printer.macros[i]);s_macros_count=~0U;update_all();}return;}
    if(action>=DO_FILE){unsigned i=action-DO_FILE;if(i<s_printer.files_count){
        bool changed=strcmp(s_selected_file,s_printer.files[i])!=0;
        snprintf(s_selected_file,sizeof(s_selected_file),"%s",s_printer.files[i]);
        for(unsigned k=0;k<s_files_count;++k)if(s_file_buttons[k])style_surface(s_file_buttons[k],k==i?0x073447:PANEL,k==i?CYAN:BORDER,6);
        if(changed||!s_metadata->valid||strcmp(s_metadata->filename,s_selected_file))klipper_client_fetch_metadata(s_selected_file);
        update_all();}return;}
    if(action>=DO_VIEW){
        if(action==DO_VIEW+PORTAL_TOOLHEAD){klipper_client_snapshot(&s_printer);if(movement_blocked()){toast("Movimiento bloqueado durante la impresion");show_view(live_view());return;}}
        show_view(action-DO_VIEW);return;
    }
    if(action==DO_AUDIO_TEST){if(audio_service_play(AUDIO_CLIP_WAKEUP)!=ESP_OK)toast("Audio no disponible o silenciado");return;}
    if(action!=DO_BACK&&action!=DO_DETAILS){uint32_t generation=s_printer.endpoint_generation;klipper_client_snapshot(&s_printer);if(generation!=s_printer.endpoint_generation){close_modal();toast("Destino cambiado; selecciona de nuevo");return;}}
    if(action>=DO_HOME_X&&action<=DO_Z_PLUS&&movement_blocked()){toast("Movimiento bloqueado durante la impresión");show_view(live_view());return;}
    switch(action){
    case DO_BACK:show_view(s_view==PORTAL_SETTINGS?live_view():s_previous);break;
    case DO_EMERGENCY:modal_open(action,"PARADA DE EMERGENCIA","¿Detener la impresora inmediatamente?\nRevisa su estado antes de continuar.",true);break;
    case DO_CANCEL:modal_open(action,"CANCELAR IMPRESIÓN","¿Cancelar el trabajo actual?\nNo se puede reanudar tras cancelar.",true);break;
    case DO_PRINT:
        if(*s_selected_file&&s_printer.available&&!s_print_pending&&s_printer.raw_state!=KLIPPER_PRINTING&&s_printer.raw_state!=KLIPPER_PAUSED){
            if(!start_print(s_selected_file,s_printer.endpoint_generation))toast("No se pudo iniciar la impresion");
        }else toast("Selecciona un archivo y espera la conexion");break;
    case DO_TUNING:device_network_snapshot(&s_network);tuning_open();break;
    case DO_RESTART:
        if(s_printer.raw_state!=KLIPPER_ERROR)toast("Klipper ya no esta en error");
        else if(queue_script("FIRMWARE_RESTART",0))audio_service_play(AUDIO_CLIP_SYSTEM);
        break;
    case DO_SHUTDOWN:modal_open(action,"APAGAR EQUIPO ANFITRIÓN","¿Apagar el equipo anfitrión?\nEl servidor Moonraker dejará de estar disponible.",true);break;
    case DO_PAUSE:if(s_printer.raw_state==KLIPPER_PRINTING&&!s_printer.pause_pending)queue_script("PAUSE",0);break;case DO_RESUME:if(s_printer.raw_state==KLIPPER_PAUSED&&!s_printer.pause_pending)queue_script("RESUME",0);break;
    case DO_MACRO:if(s_selected_macro[0]!='_'&&*s_selected_macro&&!klipper_client_action(K_ACTION_MACRO,s_selected_macro,s_printer.endpoint_generation))toast("No se pudo ejecutar la macro");break;
    case DO_EXCLUDE:if(*s_selected_object&&!klipper_client_action(K_ACTION_EXCLUDE,s_selected_object,s_printer.endpoint_generation))toast("No se pudo excluir el objeto");break;
    case DO_EDIT_HOTEND:edit_target(false);break;case DO_EDIT_BED:edit_target(true);break;
    case DO_MATERIAL_APPLY:{char cmd[64];snprintf(cmd,sizeof(cmd),"M140 S%u\nM104 S%u",MATERIAL_BED[s_material_index],MATERIAL_HOTEND[s_material_index]);queue_script(cmd,AUDIO_CLIP_TEMPERATURE);break;}
    case DO_COOL:queue_script("M104 S0\nM140 S0",AUDIO_CLIP_TEMPERATURE);break;
    case DO_HOME_X:queue_script("G28 X",AUDIO_CLIP_MOVEMENT);break;
    case DO_HOME_Y:queue_script("G28 Y",AUDIO_CLIP_MOVEMENT);break;
    case DO_HOME_Z:queue_script("G28 Z",AUDIO_CLIP_MOVEMENT);break;
    case DO_HOME_ALL:queue_script("G28",AUDIO_CLIP_MOVEMENT);break;
    case DO_MOTORS_OFF:queue_script("M84",AUDIO_CLIP_MOVEMENT);break;
    case DO_X_MINUS:case DO_X_PLUS:case DO_Y_MINUS:case DO_Y_PLUS:case DO_Z_MINUS:case DO_Z_PLUS:jog(action);break;
    case DO_EXTRUDE:case DO_RETRACT:{
        char script[128];
        int distance=action==DO_EXTRUDE?FILAMENT_STEP_MM:-FILAMENT_STEP_MM;
        snprintf(script,sizeof(script),"SAVE_GCODE_STATE NAME=JC_PANEL_E\nM83\nG1 E%d F300\nRESTORE_GCODE_STATE NAME=JC_PANEL_E MOVE=0",distance);
        queue_script(script,AUDIO_CLIP_FILAMENT);break;}
    case DO_DETAILS:s_details_last=false;s_previous=s_view;show_view(PORTAL_DETAILS);break;
    case DO_LAST_JOB:s_details_last=true;s_previous=s_view;show_view(PORTAL_DETAILS);break;
    case DO_RESET_JOB:{
        klipper_last_job_t current;device_network_snapshot_t net;device_network_snapshot(&net);
        if(!s_reset_pending&&s_details_last&&klipper_client_last_job(&current)&&current.job_id==s_last_job.job_id&&current.endpoint_generation==net.endpoint_generation&&s_printer.available&&s_printer.job_id==current.job_id&&s_printer.raw_state!=KLIPPER_PRINTING&&s_printer.raw_state!=KLIPPER_PAUSED){
            char id[16];snprintf(id,sizeof(id),"%"PRIu32,current.job_id);
            if(klipper_client_action(K_ACTION_RESET_JOB,id,net.endpoint_generation)){s_reset_pending=true;enabled(s_details_reset,false);}else toast("No se pudo limpiar el estado");
        }else toast("El trabajo cambio o Klipper no esta listo");break;}
    case DO_NETWORK:portal_network_settings_open(lv_layer_top(),settings_input_blocked);break;
    default:break;
    }
}
static void time_text(char *out,size_t size,unsigned seconds)
{
    if(seconds>=3600)snprintf(out,size,"%u h %02u min",seconds/3600,(seconds%3600)/60);
    else snprintf(out,size,"%u min",seconds/60);
}
static unsigned safe_seconds(float seconds)
{
    if(!isfinite(seconds)||seconds<=0)return 0;
    if((double)seconds>=UINT32_MAX)return UINT32_MAX;
    return (unsigned)seconds;
}
static int map_pixel(double value,int maximum)
{
    if(!isfinite(value)||value<0)return 0;
    if(value>maximum)return maximum;
    return (int)value;
}
static void rebuild_lists(void)
{

    if(s_files_count!=s_printer.files_count||memcmp(s_files_cache,s_printer.files,sizeof(s_files_cache))){
        s_files_count=s_printer.files_count;memcpy(s_files_cache,s_printer.files,sizeof(s_files_cache));lv_obj_clean(s_files_list);
        memset(s_file_buttons,0,sizeof(s_file_buttons));
        if(!s_files_count)label(s_files_list,s_printer.files_loading?"Cargando...":"No hay archivos",10,15,310,&lv_font_montserrat_16,MUTED);
        for(unsigned i=0;i<s_files_count;++i){
            lv_obj_t *b=lv_button_create(s_files_list);s_file_buttons[i]=b;lv_obj_set_pos(b,0,i*86);lv_obj_set_size(b,334,80);style_surface(b,PANEL,BORDER,6);
            lv_obj_add_event_cb(b,navigation_pressing,LV_EVENT_PRESSING,NULL);
            lv_obj_add_event_cb(b,click,LV_EVENT_CLICKED,(void*)(uintptr_t)(DO_FILE+i));
            const char *name=strrchr(s_printer.files[i],'/');name=name?name+1:s_printer.files[i];
            lv_obj_t *t=label(b,name,12,12,310,&lv_font_montserrat_20,WHITE);lv_obj_set_height(t,56);lv_label_set_long_mode(t,LV_LABEL_LONG_DOT);
            if(!strcmp(s_selected_file,s_printer.files[i]))style_surface(b,0x073447,CYAN,6);
        }
    }
    if(s_macros_count!=s_printer.macros_count||memcmp(s_macros_cache,s_printer.macros,sizeof(s_macros_cache))){
        s_macros_count=s_printer.macros_count;memcpy(s_macros_cache,s_printer.macros,sizeof(s_macros_cache));lv_obj_clean(s_macros_list);
        unsigned shown=0;
        for(unsigned i=0;i<s_macros_count;++i){
            if(s_printer.macros[i][0]=='_')continue;
            lv_obj_t *b=button(s_macros_list,s_printer.macros[i],NULL,0,shown++*66,438,58,CYAN,DO_MACRO_SELECT+i);
            lv_obj_t *t=lv_obj_get_child(b,0);lv_obj_set_width(t,410);lv_label_set_long_mode(t,LV_LABEL_LONG_SCROLL_CIRCULAR);
            if(!strcmp(s_selected_macro,s_printer.macros[i]))style_surface(b,0x073447,CYAN,6);
        }
        if(!shown)label(s_macros_list,s_printer.macros_loading?"Cargando...":"No hay macros",10,15,410,&lv_font_montserrat_16,MUTED);
    }
}
static void rebuild_objects(void)
{
    float bounds[]={s_printer.bed_min_x,s_printer.bed_max_x,s_printer.bed_min_y,s_printer.bed_max_y};
    bool changed=s_objects_count!=s_printer.objects_count||memcmp(s_objects_cache,s_printer.objects,sizeof(s_objects_cache))||
        s_map_bounds_valid!=s_printer.bed_bounds_valid||memcmp(s_map_bounds,bounds,sizeof(bounds));
    if(!changed)return;
    s_objects_count=s_printer.objects_count;memcpy(s_objects_cache,s_printer.objects,sizeof(s_objects_cache));lv_obj_clean(s_objects_list);lv_obj_clean(s_map);
    s_map_bounds_valid=s_printer.bed_bounds_valid;memcpy(s_map_bounds,bounds,sizeof(bounds));
    if(s_printer.bed_bounds_valid)format(s_map_info,"CAMA: %.0f x %.0f mm",s_printer.bed_max_x-s_printer.bed_min_x,s_printer.bed_max_y-s_printer.bed_min_y);
    else set_text(s_map_info,"MAPA: LIMITES AUN NO DISPONIBLES");
    if(!s_objects_count)label(s_objects_list,s_printer.objects_loading?"Cargando...":"Sin objetos",8,12,260,&lv_font_montserrat_16,MUTED);
    for(unsigned i=0;i<s_objects_count;++i){klipper_object_t *o=&s_printer.objects[i];bool selected=!strcmp(o->name,s_selected_object);uint32_t color=o->excluded?RED:o->active?GREEN:CYAN;
        lv_obj_t *b=button(s_objects_list,o->name,NULL,0,i*63,280,55,color,DO_OBJECT+i);lv_obj_t *t=lv_obj_get_child(b,0);lv_obj_set_width(t,258);lv_obj_set_style_text_font(t,&lv_font_montserrat_16,0);lv_label_set_long_mode(t,LV_LABEL_LONG_SCROLL_CIRCULAR);
        style_surface(b,selected?0x073447:PANEL,selected?CYAN:color,6);
        if(s_printer.bed_bounds_valid&&o->bounds_valid&&isfinite(o->min_x)&&isfinite(o->max_x)&&isfinite(o->min_y)&&isfinite(o->max_y)){
            double bw=(double)s_printer.bed_max_x-s_printer.bed_min_x,bh=(double)s_printer.bed_max_y-s_printer.bed_min_y;
            if(isfinite(bw)&&isfinite(bh)&&bw>0&&bh>0){int x=map_pixel(((double)o->min_x-s_printer.bed_min_x)*308/bw,290),y=map_pixel(((double)s_printer.bed_max_y-o->max_y)*308/bh,290);
                int w=map_pixel(((double)o->max_x-o->min_x)*308/bw,308),h=map_pixel(((double)o->max_y-o->min_y)*308/bh,308);
                if(w<18)w=18;
                if(h<18)h=18;
                if(w>308-x)w=308-x;
                if(h>308-y)h=308-y;
                char n[16];snprintf(n,sizeof(n),"%u",i+1);lv_obj_t *map_button=button(s_map,n,NULL,x,y,w,h,color,DO_OBJECT+i);style_surface(map_button,selected?0x073447:PANEL,color,4);lv_obj_set_style_text_font(lv_obj_get_child(map_button,0),&lv_font_montserrat_14,0);
            }
        }
    }
}
static void thumbnail_descriptor(lv_image_dsc_t *image,const klipper_metadata_t *metadata)
{
    lv_image_cache_drop(image);memset(image,0,sizeof(*image));
    if(metadata->valid&&metadata->thumbnail_size&&metadata->thumbnail_width&&metadata->thumbnail_height){
        image->header.magic=LV_IMAGE_HEADER_MAGIC;image->header.cf=LV_COLOR_FORMAT_RAW_ALPHA;
        image->header.w=metadata->thumbnail_width;image->header.h=metadata->thumbnail_height;
        image->data_size=metadata->thumbnail_size;image->data=metadata->thumbnail_png;
        lv_image_decoder_dsc_t decoded;
        if(lv_image_decoder_open(&decoded,image,NULL)==LV_RESULT_OK)lv_image_decoder_close(&decoded);else image->data_size=0;
    }
}
static void update_metadata(void)
{
    if(s_seen_metadata!=s_printer.metadata_revision||s_seen_generation!=s_printer.endpoint_generation){
        s_seen_metadata=s_printer.metadata_revision;
        lv_image_cache_drop(&s_thumbnail);klipper_client_metadata(s_metadata);thumbnail_descriptor(&s_thumbnail,s_metadata);
        for(unsigned i=0;i<4;++i){s_previews[i].source=NULL;lv_obj_invalidate(s_previews[i].image);}
    }
    klipper_last_job_t latest;bool valid=klipper_client_last_job(&latest);
    if(!valid)memset(&latest,0,sizeof(latest));
    if(latest.revision!=s_last_job.revision||latest.endpoint_generation!=s_last_job.endpoint_generation||latest.valid!=s_last_job.valid){
        lv_image_cache_drop(&s_last_thumbnail);s_last_job=latest;
        klipper_client_last_job_metadata(s_last_metadata);thumbnail_descriptor(&s_last_thumbnail,s_last_metadata);s_previews[3].source=NULL;
    }
}
static void update_file_tools(bool valid)
{
    static uint32_t revision=~0U,generation;static bool previous_valid;
    if(revision==s_metadata->revision&&generation==s_printer.endpoint_generation&&previous_valid==valid)return;
    revision=s_metadata->revision;generation=s_printer.endpoint_generation;previous_valid=valid;lv_obj_clean(s_file_tools);
    if(!valid)return;
    unsigned row=0;
    for(unsigned i=0;i<s_metadata->tools_count;++i){
        const klipper_tool_metadata_t *tool=&s_metadata->tools[i];
        if(!tool->has_color||(tool->has_weight&&tool->weight_grams==0&&!tool->referenced))continue;
        lv_obj_t *swatch=panel(s_file_tools,10,8+row++*44,34,34);
        lv_obj_set_style_radius(swatch,LV_RADIUS_CIRCLE,0);
        lv_obj_set_style_bg_color(swatch,lv_color_hex(strtoul(tool->color+1,NULL,16)),0);
        lv_obj_set_style_border_color(swatch,lv_color_hex(MUTED),0);lv_obj_remove_flag(swatch,LV_OBJ_FLAG_CLICKABLE);
    }
}
static void update_startup_log(void)
{
    enum { LOG_LINES=12, LOG_WIDTH=160 };
    static char previous[5][LOG_WIDTH],history[LOG_LINES*LOG_WIDTH];
    static unsigned lines;
    char state[5][LOG_WIDTH];
    if(s_network.has_ip)snprintf(state[0],sizeof(state[0]),"Wi-Fi: IP %s",s_network.ip);
    else snprintf(state[0],sizeof(state[0]),"Wi-Fi: %s",s_network.wifi_ready?"esperando IP":"inicializando");
    snprintf(state[1],sizeof(state[1]),"Moonraker: %s",!*s_network.moonraker_host?"sin configurar":
             !s_network.has_ip?"esperando Wi-Fi":s_printer.socket_connected?"conectado":"conectando");
    snprintf(state[2],sizeof(state[2]),"Klipper: %s",s_printer.state==KLIPPER_ERROR?"error":
             s_printer.available?"listo":s_printer.socket_connected?"esperando estado":"esperando Moonraker");
    snprintf(state[3],sizeof(state[3]),"%s",s_network.has_ip?"":s_network.error);
    snprintf(state[4],sizeof(state[4]),"%s",s_network.has_ip&&*s_network.moonraker_host&&!s_printer.available?
             s_printer.connection_detail:"");
    bool changed=false;
    for(unsigned i=0;i<sizeof(state)/sizeof(state[0]);++i){
        state[i][strcspn(state[i],"\r\n")]=0;
        if(!strcmp(previous[i],state[i]))continue;
        strcpy(previous[i],state[i]);
        if(!*state[i])continue;
        if(lines==LOG_LINES){
            const char *next=strchr(history,'\n')+1;
            memmove(history,next,strlen(next)+1);
            --lines;
        }
        size_t used=strlen(history);
        snprintf(history+used,sizeof(history)-used,"%s%s",lines?"\n":"",state[i]);
        ++lines;
        changed=true;
    }
    if(changed){
        set_text(s_start_log,history);
        lv_obj_t *panel=lv_obj_get_parent(s_start_log);
        lv_obj_update_layout(panel);
        lv_obj_scroll_to_y(panel,lv_obj_get_scroll_y(panel)+lv_obj_get_scroll_bottom(panel),LV_ANIM_OFF);
    }
}
static void update_all(void)
{
    bool ready=s_printer.available;char time[64];
    if(s_view==PORTAL_STARTUP){
        set_text(s_start_wifi,s_network.has_ip?"Conectado":"Conectando");
        set_text(s_start_klipper,ready?"Conectado":"Esperando");
        set_text(s_start_status,!s_network.has_ip?"Conectando Wi-Fi...":!*s_network.moonraker_host?"Configura Moonraker en Ajustes":
                 ready?"Klipper conectado":s_printer.socket_connected?"Conectando con Klipper...":"Conectando con Moonraker...");
    }
    if(s_view==PORTAL_STANDBY){
        const char *title="Reposo";
        uint32_t status_color=PORTAL_PALE_BLUE;
        const lv_image_dsc_t *core=&portal_core_base;
        if(!ready){title="DESCONECTADO";status_color=PORTAL_PALE_RED;core=&portal_core_offline;}
        else if(s_print_pending){title=s_print_uncertain?"INICIO SIN CONFIRMAR":"Iniciando impresión…";}
        else if(s_printer.state==KLIPPER_COMPLETE){title="Completado";status_color=0xD8FFE1;core=&portal_core_complete;}
        else if(s_printer.state==KLIPPER_CANCELLED){title="Cancelado";status_color=PORTAL_PALE_ORANGE;core=&portal_core_cancelled;}
        set_text(s_standby_status,title);
        text_color(s_standby_status,status_color);
        if(lv_image_get_src(s_standby_core)!=core)lv_image_set_src(s_standby_core,core);
    }
    for(unsigned row=0;row<3;++row)for(unsigned k=0;k<2;++k){
        if(s_view!=(row==0?PORTAL_STANDBY:row==1?PORTAL_PRINTING:PORTAL_PAUSED))continue;
        temperature_t *t=&s_main_temps[row][k];
        if(ready){format(t->current,"%.0f",k?s_printer.bed_temperature:s_printer.hotend_temperature);format(t->target,"/ %.0f °C",k?s_printer.bed_target:s_printer.hotend_target);}
        else {set_text(t->current,"--");set_text(t->target,"/ --");}
    }
    bool job_thumbnail=s_thumbnail.data_size&&s_metadata->valid&&!strcmp(s_metadata->filename,s_printer.filename);
    for(unsigned i=0;i<2;++i){
        if(s_view!=(i?PORTAL_PAUSED:PORTAL_PRINTING))continue;
        set_text(s_print_state[i],!ready?"DESCONECTADO":i?"EN PAUSA":"IMPRIMIENDO");
        set_text(s_print_file[i],*s_printer.filename?s_printer.filename:"---");format(s_progress_text[i],ready?"%.0f %%":"-- %%",s_printer.progress);
        update_preview(&s_previews[i],job_thumbnail?&s_thumbnail:NULL,s_printer.metadata_loading);
    }
    if(s_view==PORTAL_TOOLHEAD){
        for(unsigned i=0;i<2;++i){if(ready){format(s_tool_current[i],"%.0f °C",i?s_printer.bed_temperature:s_printer.hotend_temperature);format(s_tool_target[i],"%.0f °C",i?s_printer.bed_target:s_printer.hotend_target);}else{set_text(s_tool_current[i],"-- °C");set_text(s_tool_target[i],"-- °C");}}
        const float positions[]={s_printer.position_x,s_printer.position_y,s_printer.position_z};
        const bool homed[]={s_printer.homed_x,s_printer.homed_y,s_printer.homed_z};
        for(unsigned i=0;i<3;++i){
            float value=positions[i];
            if(!ready||!homed[i]||!isfinite(value))format(s_axis_position[i],"%c/ --","XYZ"[i]);
            else{
                char reading[32];
                int len=snprintf(reading,sizeof(reading),"%c/ %.1f","XYZ"[i],value);
                if(len>2&&len<(int)sizeof(reading)&&!strcmp(reading+len-2,".0"))reading[len-2]='\0';
                set_text(s_axis_position[i],reading);
            }
        }
    }
    bool visible_macro=false;for(unsigned i=0;i<s_printer.macros_count;++i)if(s_printer.macros[i][0]!='_'&&!strcmp(s_selected_macro,s_printer.macros[i]))visible_macro=true;
    if(!visible_macro)*s_selected_macro=0;
    rebuild_lists();rebuild_objects();
    if(s_view==PORTAL_MACROS){set_text(s_macro_name,*s_selected_macro?s_selected_macro:"---");enabled(s_macro_run,ready&&*s_selected_macro);}
    bool selected_excluded=false;for(unsigned i=0;i<s_printer.objects_count;++i)if(!strcmp(s_printer.objects[i].name,s_selected_object))selected_excluded=s_printer.objects[i].excluded;
    if(s_view==PORTAL_EXCLUDE){
        lv_color_t fill=lv_color_hex(*s_selected_object?PORTAL_BUTTON_GRAY:0xFFFFFF);
        if(!lv_color_eq(lv_obj_get_style_bg_color(s_exclude_run,0),fill))
            lv_obj_set_style_bg_color(s_exclude_run,fill,0);
        enabled(s_exclude_run,ready&&*s_selected_object&&!selected_excluded);
    }
    enabled(s_file_print,ready&&*s_selected_file&&!s_print_pending&&s_printer.raw_state!=KLIPPER_PRINTING&&s_printer.raw_state!=KLIPPER_PAUSED);
    if(s_view==PORTAL_FILES){
    set_text(s_file_title,*s_selected_file?s_selected_file:"Selecciona un archivo");
    bool file_meta=s_metadata->valid&&!strcmp(s_metadata->filename,s_selected_file);
    update_preview(&s_previews[2],file_meta&&s_thumbnail.data_size?&s_thumbnail:NULL,s_printer.metadata_loading);
    update_file_tools(file_meta);
    }
    if(s_view==PORTAL_DETAILS){
    bool details_valid=s_details_last?s_last_job.valid:*s_printer.filename!=0;
    const char *details_file=s_details_last?s_last_job.filename:s_printer.filename;
    const klipper_metadata_t *details=s_details_last?s_last_metadata:s_metadata;
    bool details_meta=details_valid&&details->valid&&!strcmp(details->filename,details_file);
    set_text(s_details_file,details_valid?details_file:"Sin trabajos anteriores");
    update_preview(&s_previews[3],details_meta?(s_details_last?&s_last_thumbnail:&s_thumbnail):NULL,!s_details_last&&s_printer.metadata_loading);
    klipper_state_t state=s_details_last?s_last_job.result:s_printer.state;
    const char *result=!details_valid?"SIN TRABAJOS ANTERIORES":state==KLIPPER_COMPLETE?"COMPLETADO":state==KLIPPER_CANCELLED?"CANCELADO":state==KLIPPER_ERROR?"FALLIDO":state==KLIPPER_PAUSED?"EN PAUSA":state==KLIPPER_PRINTING?"IMPRIMIENDO":"SIN RESULTADO";
    set_text(s_details_result,result);text_color(s_details_result,state==KLIPPER_COMPLETE?GREEN:state==KLIPPER_ERROR||state==KLIPPER_CANCELLED?RED:CYAN);
    if(details_meta){
        if(details->has_estimated_time){time_text(time,sizeof(time),safe_seconds(details->estimated_seconds));set_text(s_details_time,time);}else set_text(s_details_time,"---");
        if(details->has_weight&&details->has_length)format(s_details_weight,"%.1f g / %.1f m",details->weight_grams,details->filament_length_mm/1000);
        else if(details->has_weight)format(s_details_weight,"%.1f g",details->weight_grams);else if(details->has_length)format(s_details_weight,"%.1f m",details->filament_length_mm/1000);else set_text(s_details_weight,"---");
        unsigned active=0;for(unsigned i=0;i<details->tools_count;++i)if(!details->tools[i].has_weight||details->tools[i].weight_grams>0||details->tools[i].referenced)++active;
        format(s_details_colors,active==1?"1 filamento (consumo total)":active?"%u filamentos":"Sin desglose disponible",active);
    }else{set_text(s_details_time,"---");set_text(s_details_weight,"---");set_text(s_details_colors,"Sin desglose disponible");}
    }
    // Keep action availability current even on hidden views; LVGL ignores no-op
    // state changes, and this preserves the established disabled-state transitions.
    bool terminal=s_printer.raw_state==KLIPPER_COMPLETE||s_printer.raw_state==KLIPPER_CANCELLED||s_printer.raw_state==KLIPPER_ERROR;
    enabled(s_details_reset,s_details_last&&s_last_job.valid&&ready&&terminal&&!s_reset_pending&&s_last_job.job_id==s_printer.job_id&&s_last_job.endpoint_generation==s_printer.endpoint_generation);
    if(s_view==PORTAL_ERROR)set_text(s_error_text,*s_printer.error?s_printer.error:s_printer.connection_detail);
    if(s_view==PORTAL_SETTINGS){
        update_audio_settings();
        format(s_network_text,"%s\n%s:%u",s_network.has_ip?s_network.ip:"Sin IP",*s_network.moonraker_host?s_network.moonraker_host:"sin configurar",s_network.moonraker_port?s_network.moonraker_port:7125);
    }
    tuning_update();
}
static portal_view_t printer_view(void)
{
    return s_printer.state==KLIPPER_ERROR?PORTAL_ERROR:s_printer.state==KLIPPER_PRINTING?PORTAL_PRINTING:s_printer.state==KLIPPER_PAUSED?PORTAL_PAUSED:PORTAL_STANDBY;
}
static void show_view(portal_view_t view)
{
    if(view>=PORTAL_VIEW_COUNT)return;
    s_navigation.active=false;
    if(view!=s_view&&s_input&&lv_indev_get_state(s_input)==LV_INDEV_STATE_PRESSED){
        /* A backend-driven screen change must not release into an old control. */
        lv_indev_reset(s_input,NULL);lv_indev_wait_release(s_input);
    }
    if(view==PORTAL_TOOLHEAD&&!s_inspecting&&movement_blocked())view=live_view();
    portal_network_settings_close();
    // Rejected/uncertain results remain visible until acknowledged, even if a
    // status delta changes the screen underneath. Endpoint switches close them.
    if(!s_modal||s_modal_action!=0)close_modal();
    if(view==PORTAL_FILES)klipper_client_fetch_files();
    else if(view==PORTAL_MACROS)klipper_client_fetch_macros();
    else if(view==PORTAL_EXCLUDE)klipper_client_fetch_objects();
    if((view==PORTAL_DETAILS&&!s_details_last)||view==PORTAL_PRINTING||view==PORTAL_PAUSED){if(*s_printer.filename)klipper_client_fetch_metadata(s_printer.filename);}
    if(view!=PORTAL_DETAILS && view!=PORTAL_PRINTING && view!=PORTAL_PAUSED && view!=PORTAL_STANDBY && view!=PORTAL_ERROR)s_previous=live_view();
    s_adjustment_screen=view==PORTAL_SETTINGS;
    s_view=view;lv_screen_load(s_screens[view]);update_all();inspection_publish();
}
static portal_view_t live_view(void)
{
    return !s_have_connected&&!s_printer.available&&s_printer.state!=KLIPPER_ERROR
        ?PORTAL_STARTUP:printer_view();
}
static void inspection_end(const char *reason)
{
    if(!s_inspecting)return;
    s_inspecting=false;
    lv_obj_add_flag(s_inspection_shield,LV_OBJ_FLAG_HIDDEN);
    if(s_input){lv_indev_reset(s_input,NULL);lv_indev_wait_release(s_input);}
    lv_display_trigger_activity(s_display);
    show_view(live_view());
    printf("SCREEN_LIVE view=%s reason=%s\n",VIEW_NAMES[s_view],reason);
}
static void inspection_process_request(void)
{
    portENTER_CRITICAL(&s_inspection_lock);
    bool pending=s_inspection_request_pending;
    int target=s_inspection_target;
    uint32_t id=s_inspection_status.requested;
    s_inspection_request_pending=false;
    portEXIT_CRITICAL(&s_inspection_lock);
    if(!pending)return;
    bool refused=target>=0&&s_modal&&s_modal_action==0;
    if(refused){
        // An unacknowledged rejected/uncertain command must remain visible.
        printf("SCREEN_REFUSED id=%"PRIu32" reason=unacknowledged_result\n",id);
    }else if(target<0){
        inspection_end("console");
    }else{
        s_inspecting=true;s_adjusting_brightness=s_adjusting_volume=false;
        close_modal();
        if(s_input){lv_indev_reset(s_input,NULL);lv_indev_wait_release(s_input);}
        wake_display();
        lv_display_trigger_activity(s_display);
        show_view((portal_view_t)target);
        format(s_inspection_label,"INSPECCION: %s | SOLO LECTURA | SERIAL: 0 / M",VIEW_NAMES[target]);
        lv_obj_remove_flag(s_inspection_shield,LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_inspection_shield);
    }
    portENTER_CRITICAL(&s_inspection_lock);
    s_inspection_status.applied=id;
    portEXIT_CRITICAL(&s_inspection_lock);
    inspection_publish();
    if(!refused)printf("SCREEN_APPLIED id=%"PRIu32" active=%d view=%s\n",id,s_inspecting,VIEW_NAMES[s_view]);
}
static void input_event(lv_event_t *event)
{
    lv_event_code_t code=lv_event_get_code(event);
    if(code==LV_EVENT_PRESSED){
        memset(&s_navigation,0,sizeof(s_navigation));
        if(portal_saver_active()){
            lv_indev_reset(s_input,NULL);
            lv_indev_wait_release(s_input);
            wake_display();
            return;
        }
        lv_obj_t *owner=lv_event_get_param(event);
        if(settings_input_blocked()||!owner||lv_indev_get_type(s_input)!=LV_INDEV_TYPE_POINTER||
           lv_obj_get_screen(owner)!=lv_screen_active()||navigation_control(owner))return;
        s_navigation.owner=owner;s_navigation.view=s_view;s_navigation.active=true;
        lv_indev_get_point(s_input,&s_navigation.start);
    }else if(code==LV_EVENT_RELEASED){
        navigation_update(s_input);
        if(s_navigation.active&&lv_event_get_param(event)==s_navigation.owner&&navigation_secondary()&&
           s_navigation.direction==NAV_RIGHT&&s_navigation.delta.x>=NAV_BACK_DISTANCE&&
           s_navigation.delta.x>=2*abs(s_navigation.delta.y)){
            /* Reset before changing screens so release cannot click the old row. */
            lv_indev_reset(s_input,NULL);show_view(s_view==PORTAL_SETTINGS?live_view():s_previous);
        }
    }else if((code==LV_EVENT_SHORT_CLICKED||code==LV_EVENT_CLICKED||code==LV_EVENT_LONG_PRESSED||
              code==LV_EVENT_LONG_PRESSED_REPEAT)&&s_navigation.active&&s_navigation.moved){
        lv_indev_stop_processing(s_input);
    }else if(code==LV_EVENT_INDEV_RESET){
        s_navigation.active=false;
    }
}
void portal_ui_init(lv_display_t *display,lv_indev_t *input)
{
    s_display=display;s_input=input;
    // Keep decoded PNG cores available across partial refreshes. Entries are lazy and bounded.
    lv_image_cache_resize(3 * 255 * 304 * 4, false);
    s_metadata=calloc(1,sizeof(*s_metadata));s_last_metadata=calloc(1,sizeof(*s_last_metadata));
    LV_ASSERT_MALLOC(s_metadata);LV_ASSERT_MALLOC(s_last_metadata);
    for(unsigned i=0;i<PORTAL_VIEW_COUNT;++i)create_screen(i);
    create_startup();create_standby();create_printing(false);create_printing(true);create_toolhead();create_files();create_macros();create_exclude();create_settings();create_error();create_details();
    s_toast=large_panel(lv_layer_top(),157,412,486,49);lv_obj_set_style_border_color(s_toast,lv_color_hex(CYAN),0);s_toast_text=center_label(s_toast,"",10,11,466,&lv_font_montserrat_16,PORTAL_TEXT_COLOR);lv_obj_add_flag(s_toast,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(s_toast,LV_OBJ_FLAG_CLICKABLE);
    s_inspection_shield=lv_obj_create(lv_layer_top());lv_obj_remove_style_all(s_inspection_shield);
    lv_obj_set_size(s_inspection_shield,800,480);
    lv_obj_add_flag(s_inspection_shield,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_inspection_shield,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_EVENT_BUBBLE|LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_t *inspection_banner=large_panel(s_inspection_shield,144,226,512,28);
    lv_obj_set_style_border_color(inspection_banner,lv_color_hex(CYAN),0);
    s_inspection_label=center_label(inspection_banner,"",4,5,502,&lv_font_montserrat_14,CYAN);
    if(input)lv_indev_add_event_cb(input,input_event,LV_EVENT_ALL,NULL);
    apply_brightness(s_brightness);lv_screen_load(s_screens[PORTAL_STARTUP]);
}
void portal_ui_tick(void)
{
    int64_t now=esp_timer_get_time();
    portal_saver_tick();
    if(s_toast_until&&now>=s_toast_until){lv_obj_add_flag(s_toast,LV_OBJ_FLAG_HIDDEN);s_toast_until=0;}
    if(now<s_next_tick)return;
    s_next_tick=now+200000;
    device_network_snapshot(&s_network);klipper_client_snapshot(&s_printer);
    if(!s_have_connected||s_view==PORTAL_STARTUP)update_startup_log();
    audio_events_update(&s_printer);
    portal_network_settings_tick();
    device_network_display_status_t display_save;device_network_display_status(&display_save);
    static uint32_t seen_display_save;
    if(display_save.completed_revision!=seen_display_save){
        seen_display_save=display_save.completed_revision;
        if(!display_save.pending&&display_save.result!=ESP_OK)toast("No se pudieron guardar los ajustes de pantalla");
    }
    static unsigned previous_state=~0U;static bool previous_available,previous_socket;
    if(previous_state!=(unsigned)s_printer.state||previous_available!=s_printer.available||previous_socket!=s_printer.socket_connected){
        ESP_LOGI("portal","PORTAL_STATE state=%u available=%u websocket=%u snapshot=%u generation=%"PRIu32,
            (unsigned)s_printer.state,s_printer.available,s_printer.socket_connected,s_printer.initial_snapshot,s_printer.endpoint_generation);
        previous_state=s_printer.state;previous_available=s_printer.available;previous_socket=s_printer.socket_connected;
    }
    if(s_seen_generation!=s_printer.endpoint_generation){close_modal();*s_selected_file=0;*s_selected_macro=0;*s_selected_object=0;*s_last_job_file=0;s_files_count=s_macros_count=s_objects_count=~0U;s_print_pending=s_reset_pending=s_print_uncertain=false;}
    bool pending_job_finished=s_printer.job_id!=s_print_pending_job&&
        !strcmp(s_printer.filename,s_print_pending_file)&&
        (s_printer.raw_state==KLIPPER_COMPLETE||s_printer.raw_state==KLIPPER_CANCELLED);
    if(s_print_pending&&(s_printer.raw_state==KLIPPER_PRINTING||s_printer.raw_state==KLIPPER_PAUSED||s_printer.raw_state==KLIPPER_ERROR||pending_job_finished))s_print_pending=s_print_uncertain=false;
    update_metadata();s_seen_generation=s_printer.endpoint_generation;
    if(strcmp(s_last_job_file,s_printer.filename)){
        snprintf(s_last_job_file,sizeof(s_last_job_file),"%s",s_printer.filename);
        if(*s_last_job_file&&s_view!=PORTAL_FILES&&(s_printer.state==KLIPPER_PRINTING||s_printer.state==KLIPPER_PAUSED))klipper_client_fetch_metadata(s_last_job_file);
    }
    if(!s_adjusting_brightness&&s_network.brightness>=5&&s_network.brightness!=s_brightness){s_brightness=s_network.brightness;apply_brightness(s_brightness);lv_slider_set_value(s_brightness_slider,s_brightness,LV_ANIM_OFF);format(s_brightness_text,"%u %%",s_brightness);}
    if(s_network.sleep_minutes!=s_sleep_minutes){s_sleep_minutes=s_network.sleep_minutes;lv_dropdown_set_selected(s_timeout,s_sleep_minutes==1?0:s_sleep_minutes==5?1:2);}
    inspection_process_request();
    if(s_inspecting){
        if(s_printer.available)s_have_connected=true;
        // Keep settings truthful even if an event was already being dispatched
        // when the serial request arrived. Only stored/live values are shown.
        lv_slider_set_value(s_brightness_slider,s_brightness,LV_ANIM_OFF);
        lv_dropdown_set_selected(s_timeout,s_sleep_minutes==1?0:s_sleep_minutes==5?1:2);
        lv_dropdown_set_selected(s_material,s_material_index);
        lv_dropdown_set_selected(s_step,s_step_index);
    }
    else{
        if(s_view==PORTAL_TOOLHEAD&&movement_blocked())show_view(live_view());
        if(!s_have_connected&&s_printer.state==KLIPPER_ERROR&&s_view==PORTAL_STARTUP)show_view(PORTAL_ERROR);
        if(!s_have_connected&&!s_printer.available&&s_printer.state!=KLIPPER_ERROR&&s_view==PORTAL_ERROR)show_view(PORTAL_STARTUP);
        if(s_printer.available&&!s_have_connected){s_have_connected=true;if(!s_adjustment_screen)show_view(printer_view());}
        else if(s_have_connected){if(s_printer.state==KLIPPER_ERROR&&s_view!=PORTAL_ERROR&&!s_adjustment_screen)show_view(PORTAL_ERROR);
            else if(s_view==PORTAL_STANDBY||s_view==PORTAL_PRINTING||s_view==PORTAL_PAUSED||s_view==PORTAL_ERROR){portal_view_t next=printer_view();if(next!=s_view)show_view(next);}}
    }
    update_all();
    static int64_t next_objects;
    if(s_view==PORTAL_EXCLUDE&&now>=next_objects){klipper_client_fetch_objects();next_objects=now+5000000;}
    klipper_command_result_t result;unsigned budget=8;
    while(budget--&&klipper_client_pop_result(&result)){
        ESP_LOGI("portal","COMMAND_RESULT id=%"PRIu32" status=%u",result.id,(unsigned)result.status);
        bool print_result=strstr(result.command,"SDCARD_PRINT_FILE")!=NULL;
        if(print_result&&result.status==KLIPPER_REJECTED)s_print_pending=s_print_uncertain=false;
        if(print_result&&result.status==KLIPPER_UNKNOWN&&s_print_pending)s_print_uncertain=true;
        if(s_modal_action==DO_TUNING&&result.status!=KLIPPER_PENDING){
            if(strstr(result.command,"M220"))s_tuning_pending[0]=false;
            if(strstr(result.command,"M221"))s_tuning_pending[1]=false;
            if(strstr(result.command,"M106"))s_tuning_pending[2]=false;
        }
        if(strstr(result.command,"SDCARD_RESET_FILE")&&result.status!=KLIPPER_PENDING){s_reset_pending=false;if(result.status==KLIPPER_COMPLETED)show_view(live_view());}
        if(result.status==KLIPPER_REJECTED||result.status==KLIPPER_UNKNOWN){
            inspection_end("command_result");
            if(s_modal_action==DO_TUNING)close_modal();
            if(!s_modal)modal_open(0,result.status==KLIPPER_REJECTED?"ORDEN RECHAZADA":"RESULTADO INCIERTO",result.message,true);
            else toast(result.status==KLIPPER_REJECTED?"Orden rechazada":"Resultado incierto; revisa la impresora");
        }else if(result.status==KLIPPER_COMPLETED){
            if(result.generation==s_printer.endpoint_generation &&
               result.context>=AUDIO_CLIP_TEMPERATURE && result.context<=AUDIO_CLIP_FILAMENT)
                audio_service_play_printer((audio_clip_t)result.context);
            if(!print_result||!s_print_pending)toast("Orden aceptada");
        }
        else if(!print_result)toast("Orden en espera");
    }
    if(s_inspecting){lv_display_trigger_activity(s_display);}
    else if(portal_saver_active()){
        portal_saver_update(&s_printer);
    }else if(s_view!=PORTAL_STARTUP&&s_input&&s_sleep_minutes&&lv_indev_get_state(s_input)!=LV_INDEV_STATE_PRESSED&&
             lv_display_get_inactive_time(s_display)>=s_sleep_minutes*60000UL){
        portal_network_settings_close();
        if(s_modal&&s_modal_action!=0)close_modal();
        portal_saver_show(lv_display_get_layer_sys(s_display),&s_printer);
        apply_brightness(s_brightness);
    }
}
#ifdef PORTAL_UI_HOST_TEST
void portal_ui_test_show(portal_view_t view)
{
    device_network_snapshot(&s_network);klipper_client_snapshot(&s_printer);update_metadata();s_seen_generation=s_printer.endpoint_generation;
    if(s_printer.files_count)snprintf(s_selected_file,sizeof(s_selected_file),"%s",s_printer.files[0]);
    if(s_printer.macros_count)snprintf(s_selected_macro,sizeof(s_selected_macro),"%s",s_printer.macros[0]);
    if(s_printer.objects_count)snprintf(s_selected_object,sizeof(s_selected_object),"%s",s_printer.objects[0].name);
    show_view(view);
}
#endif
