#include "portal_saver.h"
#include "portal_assets.h"
#include "portal_core.h"
#include <stdio.h>
#include <string.h>

#define SAVER_BG 0x0A1118
#define SAVER_TEXT 0xA0B1BA
#define SAVER_FILE 0x758A93
#define SAVER_TIME_CAPTION 0x788F9B
#define SAVER_ELAPSED 0x879EA9
#define SAVER_TEMPERATURE 0x8299A3
#define SAVER_HINT 0x718692
#define SAVER_BLUE 0x67BFE6
#define SAVER_AMBER 0xE5B77C
#define SAVER_GREEN 0x8FCE9E
#define SAVER_RED 0xEF9A92
#define SAVER_TITLE_BLUE 0x6D9EAE
#define SAVER_TITLE_AMBER 0xAE8F69
#define SAVER_TITLE_GREEN 0x779D87
#define SAVER_TITLE_RED 0xB67B77
#define SAVER_IDLE_PROMPT 0x084F6A
#define SAVER_ERROR_FOOTER 0x80350E

enum { CORE_X = 52, CORE_Y = 85 };

typedef struct {
    const char *title;
    uint32_t text_color, progress_color;
    portal_saver_face_t face;
    int lid;
} status_t;

static const status_t s_statuses[] = {
    [KLIPPER_STARTUP] = {"CONECTANDO", SAVER_TITLE_BLUE, SAVER_BLUE, PORTAL_SAVER_FACE_BLUE, 800},
    [KLIPPER_OFFLINE] = {"DESCONECTADO", SAVER_TITLE_AMBER, SAVER_AMBER, PORTAL_SAVER_FACE_BLUE, 1400},
    [KLIPPER_STANDBY] = {"En espera", SAVER_TITLE_BLUE, SAVER_BLUE, PORTAL_SAVER_FACE_BLUE, 0},
    [KLIPPER_PRINTING] = {"IMPRIMIENDO", SAVER_TITLE_BLUE, SAVER_BLUE, PORTAL_SAVER_FACE_BLUE, 0},
    [KLIPPER_PAUSED] = {"EN PAUSA", SAVER_TITLE_AMBER, SAVER_AMBER, PORTAL_SAVER_FACE_BLUE, 800},
    [KLIPPER_COMPLETE] = {"COMPLETADO", SAVER_TITLE_GREEN, SAVER_GREEN, PORTAL_SAVER_FACE_GREEN, -400},
    [KLIPPER_CANCELLED] = {"CANCELADO", SAVER_TITLE_AMBER, SAVER_AMBER, PORTAL_SAVER_FACE_YELLOW, 400},
    [KLIPPER_ERROR] = {"ERROR DE KLIPPER", SAVER_TITLE_RED, SAVER_RED, PORTAL_SAVER_FACE_RED, 200},
};

static lv_obj_t *s_root, *s_title, *s_file, *s_percent, *s_track, *s_fill;
static lv_obj_t *s_time_group, *s_remaining, *s_elapsed, *s_nozzle, *s_bed, *s_reason;
static lv_obj_t *s_idle_prompt, *s_error_footer;
static bool s_active;
static int s_state, s_progress;

static lv_obj_t *surface(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, w, h);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    return object;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int width,
                       const lv_font_t *font, uint32_t color)
{
    lv_obj_t *object = lv_label_create(parent);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_width(object, width);
    lv_obj_set_style_text_font(object, portal_font(font), 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    lv_label_set_text(object, text);
    return object;
}

static void set_text(lv_obj_t *object, const char *text)
{
    if(strcmp(lv_label_get_text(object), text)) lv_label_set_text(object, text);
}

static void visible(lv_obj_t *object, bool show)
{
    if(show) lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
}

static void create(lv_obj_t *layer)
{
    s_root = surface(layer, 0, 0, 800, 480, SAVER_BG);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    portal_core_saver_create(s_root, CORE_X, CORE_Y);

    s_title = label(s_root, "", 342, 74, 406, &lv_font_montserrat_28, SAVER_TITLE_BLUE);
    s_idle_prompt = label(s_root, "Que quieres\nimprimir ahora?", 365, 186, 383,
                          &lv_font_montserrat_42, SAVER_IDLE_PROMPT);
    lv_obj_set_style_text_line_space(s_idle_prompt, 4, 0);
    s_file = label(s_root, "", 342, 109, 406, &lv_font_montserrat_20, SAVER_FILE);
    lv_label_set_long_mode(s_file, LV_LABEL_LONG_DOT);
    s_percent = label(s_root, "", 336, 137, 412, &portal_saver_progress_font, SAVER_TEXT);
    s_track = surface(s_root, 342, 266, 406, 7, 0x263746);
    s_fill = surface(s_track, 0, 0, 0, 7, SAVER_BLUE);

    s_time_group = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_time_group);
    lv_obj_remove_flag(s_time_group, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_time_group, 342, 295);
    lv_obj_set_size(s_time_group, 406, 115);
    label(s_time_group, "RESTANTE ESTIMADO", 0, 3, 210, &lv_font_montserrat_16, SAVER_TIME_CAPTION);
    label(s_time_group, "TRANSCURRIDO", 220, 3, 186, &lv_font_montserrat_16, SAVER_TIME_CAPTION);
    s_remaining = label(s_time_group, "", 0, 30, 210, &lv_font_montserrat_28, SAVER_TEXT);
    s_elapsed = label(s_time_group, "", 220, 30, 186, &lv_font_montserrat_28, SAVER_ELAPSED);
    s_nozzle = label(s_time_group, "", 0, 94, 210, &lv_font_montserrat_20, SAVER_TEMPERATURE);
    s_bed = label(s_time_group, "", 220, 94, 186, &lv_font_montserrat_20, SAVER_TEMPERATURE);

    s_reason = label(s_root, "", 342, 173, 406, &lv_font_montserrat_20, SAVER_TEXT);
    lv_obj_set_height(s_reason, 231);
    lv_label_set_long_mode(s_reason, LV_LABEL_LONG_DOT);
    s_error_footer = label(s_root, "The cake is a lie", 344, 406, 404,
                           &lv_font_montserrat_20, SAVER_ERROR_FOOTER);
    lv_obj_t *hint = label(s_root, "Toca para volver", 250, 442, 300, &lv_font_montserrat_16, SAVER_HINT);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
}

void portal_saver_tick(void)
{
    if(s_active) portal_core_saver_tick();
}

static void duration(lv_obj_t *object, uint32_t seconds, bool valid)
{
    char text[32];
    if(!valid) {
        set_text(object, "--");
        return;
    }
    unsigned minutes = seconds / 60;
    if(minutes >= 60) snprintf(text, sizeof(text), "%u h %02u min", minutes / 60, minutes % 60);
    else snprintf(text, sizeof(text), "%u min", minutes);
    set_text(object, text);
}

void portal_saver_update(const klipper_snapshot_t *printer)
{
    if(!s_active) return;
    klipper_state_t state = printer->state;
    if(!printer->available && state != KLIPPER_ERROR) state = KLIPPER_OFFLINE;
    const status_t *status = &s_statuses[state];
    bool error = state == KLIPPER_ERROR;
    bool connected = printer->available;
    bool active_job = state == KLIPPER_PRINTING || state == KLIPPER_PAUSED;
    bool finished = state == KLIPPER_COMPLETE || state == KLIPPER_CANCELLED;
    bool has_job = connected && (active_job || finished);
    if(s_state != (int)state) {
        bool first = s_state < 0;
        s_state = state;
        portal_core_saver_set_face(status->face, status->lid, first);
        set_text(s_title, status->title);
        lv_obj_set_pos(s_title, state == KLIPPER_STANDBY ? 365 : 342,
                       state == KLIPPER_STANDBY ? 86 : 74);
        lv_obj_set_style_text_color(s_title, lv_color_hex(status->text_color), 0);
        lv_obj_set_style_bg_color(s_fill, lv_color_hex(status->progress_color), 0);
        visible(s_idle_prompt, state == KLIPPER_STANDBY);
        visible(s_error_footer, error);
        visible(s_file, has_job || error);
        visible(s_reason, error || state == KLIPPER_OFFLINE);
        visible(s_percent, has_job);
        visible(s_track, has_job);
        visible(s_time_group, has_job);
    }
    if(error || state == KLIPPER_OFFLINE) {
        set_text(s_reason, error && *printer->error ? printer->error : printer->connection_detail);
    }
    if(has_job || error) set_text(s_file, printer->filename);
    if(!has_job) return;
    float value = printer->progress;
    if(!(value >= 0)) value = 0;
    if(value > 100) value = 100;
    int progress = state == KLIPPER_COMPLETE ? 100 : (int)(value + .5f);
    if(progress != s_progress) {
        char text[16];
        s_progress = progress;
        snprintf(text, sizeof(text), "%d %%", progress);
        set_text(s_percent, text);
        lv_obj_set_width(s_fill, 406 * progress / 100);
    }
    duration(s_remaining, state == KLIPPER_COMPLETE ? 0 : printer->time_remaining,
             state == KLIPPER_COMPLETE || (active_job && value > .5f && printer->print_duration));
    duration(s_elapsed, printer->print_duration, true);
    char temperature[32];
    snprintf(temperature, sizeof(temperature), "Boquilla  %.0f °C", printer->hotend_temperature);
    set_text(s_nozzle, temperature);
    snprintf(temperature, sizeof(temperature), "Cama  %.0f °C", printer->bed_temperature);
    set_text(s_bed, temperature);
}

bool portal_saver_active(void)
{
    return s_active;
}

void portal_saver_show(lv_obj_t *layer, const klipper_snapshot_t *printer)
{
    if(!s_root) create(layer);
    s_active = true;
    s_state = s_progress = -1;
    portal_saver_update(printer);
    visible(s_root, true);
}

void portal_saver_hide(void)
{
    if(!s_active) return;
    s_active = false;
    portal_core_saver_stop();
    visible(s_root, false);
}
