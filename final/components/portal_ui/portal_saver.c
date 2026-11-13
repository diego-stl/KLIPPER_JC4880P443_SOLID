#include "portal_saver.h"
#include "portal_assets.h"
#ifndef PORTAL_UI_HOST_TEST
#include "esp_random.h"
#endif
#include <stdio.h>
#include <string.h>

#define SAVER_BG 0x0A1118
#define SAVER_TEXT 0xDCE8ED
#define SAVER_MUTED 0x9AAEB9
#define SAVER_BLUE 0x67BFE6
#define SAVER_AMBER 0xE5B77C
#define SAVER_GREEN 0x8FCE9E
#define SAVER_RED 0xEF9A92

enum { VISOR_W = 136, VISOR_H = 135, CORE_X = 52, CORE_Y = 85 };
enum { MOVE_MS = 400, FIRST_LOOK_MS = 1200, MIN_LOOK_MS = 1450, MAX_LOOK_MS = 3400 };
enum { POSE_SCALE = 10000, ANIMATION_MAX = 1024 };
enum { BLUE, GREEN, YELLOW, RED };

typedef struct {
    int x, y;
    int upper_left, upper_right, lower_left, lower_right;
} pose_t;

/* Coordinates in hundredths of a percent of the visor, from the simulator. */
static const pose_t s_poses[] = {
    {4961, 4797,  979,  794, 8728, 8540},
    {5100, 3100, 1000, 1000, 7500, 7500},
    {5000, 6900, 3500, 3500, 9100, 9100},
    {2800, 5100, 1700, 2900, 8500, 8600},
    {7316, 5295, 4047, 2599, 8270, 8012},
    {2800, 3884, 1371, 2761, 7787, 8067},
    {3471, 6910, 4225, 4192, 8724, 8561},
    {6900, 3500, 2800, 1200, 8000, 7700},
    {6800, 6700, 4000, 3300, 8800, 9100},
};

typedef struct {
    const lv_image_dsc_t *iris, *lens;
} face_t;

static const face_t s_faces[] = {
    [BLUE] = {&portal_saver_iris_blue, &portal_saver_lens_blue},
    [GREEN] = {&portal_saver_iris_green, &portal_saver_lens_green},
    [YELLOW] = {&portal_saver_iris_yellow, &portal_saver_lens_yellow},
    [RED] = {&portal_saver_iris_red, &portal_saver_lens_red},
};

typedef struct {
    const char *title;
    uint32_t color;
    unsigned face;
    int lid;
} status_t;

static const status_t s_statuses[] = {
    [KLIPPER_STARTUP] = {"CONECTANDO", SAVER_BLUE, BLUE, 800},
    [KLIPPER_OFFLINE] = {"DESCONECTADO", SAVER_AMBER, BLUE, 1400},
    [KLIPPER_STANDBY] = {"REPOSO", SAVER_BLUE, BLUE, 0},
    [KLIPPER_PRINTING] = {"IMPRIMIENDO", SAVER_BLUE, BLUE, 0},
    [KLIPPER_PAUSED] = {"EN PAUSA", SAVER_AMBER, BLUE, 800},
    [KLIPPER_COMPLETE] = {"COMPLETADO", SAVER_GREEN, GREEN, -400},
    [KLIPPER_CANCELLED] = {"CANCELADO", SAVER_AMBER, YELLOW, 400},
    [KLIPPER_ERROR] = {"ERROR DE KLIPPER", SAVER_RED, RED, 200},
};

static lv_obj_t *s_root, *s_visor, *s_title, *s_file, *s_percent, *s_track, *s_fill;
static lv_obj_t *s_time_group, *s_remaining, *s_elapsed, *s_nozzle, *s_bed, *s_reason;
static bool s_active;
static int s_state, s_progress;
static uint32_t s_look_started, s_look_delay;
static unsigned s_target;
static const face_t *s_face;
static pose_t s_drawn, s_start_pose, s_end_pose;
static uint16_t s_pixels[VISOR_W * VISOR_H] __attribute__((aligned(LV_DRAW_BUF_ALIGN)));

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
    s_visor = lv_canvas_create(s_root);
    lv_obj_remove_flag(s_visor, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_visor, CORE_X + 61, CORE_Y + 85);
    lv_canvas_set_buffer(s_visor, s_pixels, VISOR_W, VISOR_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_t *shell = lv_image_create(s_root);
    lv_obj_remove_flag(shell, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(shell, CORE_X, CORE_Y);
    lv_image_set_src(shell, &portal_saver_shell);

    s_title = label(s_root, "", 342, 74, 406, &lv_font_montserrat_28, SAVER_BLUE);
    s_file = label(s_root, "", 342, 109, 406, &lv_font_montserrat_20, SAVER_MUTED);
    lv_label_set_long_mode(s_file, LV_LABEL_LONG_DOT);
    s_percent = label(s_root, "", 336, 137, 412, &portal_saver_progress_font, SAVER_TEXT);
    s_track = surface(s_root, 342, 266, 406, 7, 0x263746);
    s_fill = surface(s_track, 0, 0, 0, 7, SAVER_BLUE);

    s_time_group = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_time_group);
    lv_obj_remove_flag(s_time_group, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_time_group, 342, 295);
    lv_obj_set_size(s_time_group, 406, 115);
    label(s_time_group, "RESTANTE ESTIMADO", 0, 3, 210, &lv_font_montserrat_16, SAVER_MUTED);
    label(s_time_group, "TRANSCURRIDO", 220, 3, 186, &lv_font_montserrat_16, SAVER_MUTED);
    s_remaining = label(s_time_group, "", 0, 30, 210, &lv_font_montserrat_28, SAVER_TEXT);
    s_elapsed = label(s_time_group, "", 220, 30, 186, &lv_font_montserrat_28, SAVER_TEXT);
    s_nozzle = label(s_time_group, "", 0, 94, 210, &lv_font_montserrat_20, SAVER_MUTED);
    s_bed = label(s_time_group, "", 220, 94, 186, &lv_font_montserrat_20, SAVER_MUTED);

    s_reason = label(s_root, "", 342, 173, 406, &lv_font_montserrat_20, SAVER_TEXT);
    lv_obj_set_height(s_reason, 231);
    lv_label_set_long_mode(s_reason, LV_LABEL_LONG_DOT);
    lv_obj_t *hint = label(s_root, "Toca para volver", 250, 442, 300, &lv_font_montserrat_16, SAVER_MUTED);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
}

static void draw_lens(const pose_t *pose)
{
    const lv_image_dsc_t *lens = s_face->lens;
    int width = lens->header.w, height = lens->header.h;
    int left = pose->x * VISOR_W / POSE_SCALE - width / 2;
    int top = pose->y * VISOR_H / POSE_SCALE - height / 2;
    int x0 = left < 0 ? -left : 0;
    int y0 = top < 0 ? -top : 0;
    int x1 = left + width > VISOR_W ? VISOR_W - left : width;
    int y1 = top + height > VISOR_H ? VISOR_H - top : height;
    const uint16_t *ink = (const uint16_t *)lens->data;
    const uint8_t *alpha = lens->data + lens->header.stride * height;
    for(int y = y0; y < y1; ++y) {
        for(int x = x0; x < x1; ++x) {
            int index = y * width + x;
            uint16_t *pixel = &s_pixels[(top + y) * VISOR_W + left + x];
            if(alpha[index]) *pixel = lv_color_16_16_mix(ink[index], *pixel, alpha[index]);
        }
    }
}

static void draw_eye(const pose_t *pose)
{
    if(!s_face) return;
    memcpy(s_pixels, s_face->iris->data, sizeof(s_pixels));
    draw_lens(pose);
    /* The shell sits above this canvas and masks its rectangular corners.
     * Fill the two eyelids along interpolated chords inside the visor. */
    const uint16_t lid = lv_color_to_u16(lv_color_hex(0x393F44));
    for(int x = 0; x < VISOR_W; ++x) {
        int upper = pose->upper_left + (pose->upper_right - pose->upper_left) * x / (VISOR_W - 1);
        int lower = pose->lower_left + (pose->lower_right - pose->lower_left) * x / (VISOR_W - 1);
        upper = upper * VISOR_H * 256 / POSE_SCALE;
        lower = lower * VISOR_H * 256 / POSE_SCALE;
        for(int y = 0; y < upper / 256; ++y) s_pixels[y * VISOR_W + x] = lid;
        for(int y = lower / 256 + 1; y < VISOR_H; ++y) s_pixels[y * VISOR_W + x] = lid;
        uint16_t *upper_edge = &s_pixels[(upper / 256) * VISOR_W + x];
        uint16_t *lower_edge = &s_pixels[(lower / 256) * VISOR_W + x];
        *upper_edge = lv_color_16_16_mix(lid, *upper_edge, upper % 256);
        *lower_edge = lv_color_16_16_mix(lid, *lower_edge, 255 - lower % 256);
    }
    lv_obj_invalidate(s_visor);
}

static int lerp(int start, int end, int progress)
{
    return start + (end - start) * progress / ANIMATION_MAX;
}

static void animate_eye(void *canvas, int32_t value)
{
    (void)canvas;
    pose_t pose = {
        lerp(s_start_pose.x, s_end_pose.x, value),
        lerp(s_start_pose.y, s_end_pose.y, value),
        lerp(s_start_pose.upper_left, s_end_pose.upper_left, value),
        lerp(s_start_pose.upper_right, s_end_pose.upper_right, value),
        lerp(s_start_pose.lower_left, s_end_pose.lower_left, value),
        lerp(s_start_pose.lower_right, s_end_pose.lower_right, value),
    };
    if(!memcmp(&pose, &s_drawn, sizeof(pose))) return;
    s_drawn = pose;
    draw_eye(&s_drawn);
}

static void look(unsigned target, bool move)
{
    s_target = target;
    s_end_pose = s_poses[target];
    s_end_pose.upper_left += s_statuses[s_state].lid;
    s_end_pose.upper_right += s_statuses[s_state].lid;
    lv_anim_delete(s_visor, animate_eye);
    if(!move) {
        s_drawn = s_end_pose;
        draw_eye(&s_drawn);
        return;
    }
    s_start_pose = s_drawn;
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_visor);
    lv_anim_set_exec_cb(&anim, animate_eye);
    lv_anim_set_values(&anim, 0, ANIMATION_MAX);
    lv_anim_set_duration(&anim, MOVE_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_start(&anim);
}

static uint32_t random_between(uint32_t min, uint32_t max)
{
#ifdef PORTAL_UI_HOST_TEST
    return lv_rand(min, max);
#else
    return min + esp_random() % (max - min + 1);
#endif
}

void portal_saver_tick(void)
{
    if(!s_active || lv_tick_elaps(s_look_started) < s_look_delay) return;
    unsigned next = random_between(0, sizeof(s_poses) / sizeof(s_poses[0]) - 2);
    if(next >= s_target) ++next;
    look(next, true);
    s_look_started = lv_tick_get();
    s_look_delay = random_between(MIN_LOOK_MS, MAX_LOOK_MS);
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
        s_face = &s_faces[status->face];
        look(s_target, !first);
        if(!first) draw_eye(&s_drawn);
        set_text(s_title, status->title);
        lv_obj_set_style_text_color(s_title, lv_color_hex(status->color), 0);
        lv_obj_set_style_bg_color(s_fill, lv_color_hex(status->color), 0);
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
    s_target = 0;
    s_look_started = lv_tick_get();
    s_look_delay = FIRST_LOOK_MS;
    portal_saver_update(printer);
    visible(s_root, true);
}

void portal_saver_hide(void)
{
    if(!s_active) return;
    s_active = false;
    lv_anim_delete(s_visor, animate_eye);
    visible(s_root, false);
}
