#include "portal_core.h"
#include "portal_assets.h"
#include "esp_heap_caps.h"
#ifndef PORTAL_UI_HOST_TEST
#include "esp_random.h"
#endif
#include <string.h>

enum { SAVER_W = 136, SAVER_H = 135, CORE_W = 134, CORE_H = 134, WARNING_W = 110, WARNING_H = 110 };
enum { MOVE_MS = 400, FIRST_LOOK_MS = 1200, MIN_LOOK_MS = 1450, MAX_LOOK_MS = 3400 };
enum { POSE_SCALE = 10000, ANIMATION_MAX = 1024 };

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

typedef struct {
    const lv_image_dsc_t *shell;
    face_t face;
    pose_t rest;
    int motion;
} variant_t;

/* Keep the original resting expressions and apply the simulator's pose offsets
 * to each. The offline visor has no lens and only shifts its narrow aperture. */
static const variant_t s_variants[] = {
    [PORTAL_CORE_BASE] = {
        &portal_core_base, {&portal_core_iris_blue, &portal_core_lens_blue},
        {7410, 5370, 4150, 2620, 8220, 7900}, 52,
    },
    [PORTAL_CORE_COMPLETE] = {
        &portal_core_complete, {&portal_core_iris_green, &portal_core_lens_green},
        {5680, 3970, 1640, 1640, 5700, 7000}, 65,
    },
    [PORTAL_CORE_CANCELLED] = {
        &portal_core_cancelled, {&portal_core_iris_yellow, &portal_core_lens_yellow},
        {5100, 5220, 2920, 3440, 7100, 8900}, 60,
    },
    [PORTAL_CORE_OFFLINE] = {
        &portal_core_offline, {&portal_core_iris_offline, NULL},
        {5000, 5000, 6100, 5360, 7100, 8900}, 18,
    },
    [PORTAL_CORE_ERROR] = {
        &portal_core_error, {&portal_core_iris_red, &portal_core_lens_red},
        {7420, 5160, 4030, 4030, 7850, 8020}, 55,
    },
    [PORTAL_CORE_WARNING] = {
        &portal_core_warning, {&portal_core_iris_blue, &portal_core_lens_warning},
        {5100, 5200, 2650, 3150, 8280, 8010}, 66,
    },
};

static const face_t s_saver_faces[] = {
    [PORTAL_SAVER_FACE_BLUE] = {&portal_saver_iris_blue, &portal_saver_lens_blue},
    [PORTAL_SAVER_FACE_GREEN] = {&portal_saver_iris_green, &portal_saver_lens_green},
    [PORTAL_SAVER_FACE_YELLOW] = {&portal_saver_iris_yellow, &portal_saver_lens_yellow},
    [PORTAL_SAVER_FACE_RED] = {&portal_saver_iris_red, &portal_saver_lens_red},
};

typedef struct {
    const face_t *face;
    const variant_t *variant;
    lv_obj_t *view, *drawn_core;
    uint16_t *pixels;
    lv_image_dsc_t visor;
    int width, height, upper_offset;
    unsigned target;
    uint32_t look_started, look_delay;
    pose_t drawn, from, to;
} eye_t;

static eye_t s_normal, s_warning, s_saver;
static eye_t *s_active_core;
static uint16_t s_saver_pixels[SAVER_W * SAVER_H] __attribute__((aligned(LV_DRAW_BUF_ALIGN)));

static uint32_t random_between(uint32_t min, uint32_t max)
{
#ifdef PORTAL_UI_HOST_TEST
    return lv_rand(min, max);
#else
    return min + esp_random() % (max - min + 1);
#endif
}

static void draw_lens(const eye_t *eye, const pose_t *pose)
{
    const lv_image_dsc_t *lens = eye->face->lens;
    if(!lens) return;

    int width = lens->header.w, height = lens->header.h;
    int left = pose->x * eye->width / POSE_SCALE - width / 2;
    int top = pose->y * eye->height / POSE_SCALE - height / 2;
    int x0 = LV_MAX(0, -left), y0 = LV_MAX(0, -top);
    int x1 = LV_MIN(width, eye->width - left), y1 = LV_MIN(height, eye->height - top);
    const uint16_t *ink = (const uint16_t *)lens->data;
    const uint8_t *alpha = lens->data + lens->header.stride * height;
    for(int y = y0; y < y1; ++y) {
        for(int x = x0; x < x1; ++x) {
            int index = y * width + x;
            uint16_t *pixel = &eye->pixels[(top + y) * eye->width + left + x];
            if(alpha[index]) *pixel = lv_color_16_16_mix(ink[index], *pixel, alpha[index]);
        }
    }
}

static void visor_area(lv_obj_t *core, lv_area_t *area)
{
    lv_obj_get_coords(core, area);
    bool warning = lv_image_get_src(core) == &portal_core_warning;
    area->x1 += warning ? 51 : 62;
    area->y1 += warning ? 72 : 86;
    area->x2 = area->x1 + (warning ? WARNING_W : CORE_W) - 1;
    area->y2 = area->y1 + (warning ? WARNING_H : CORE_H) - 1;
}

static void paint_iris(const eye_t *eye)
{
    const lv_image_dsc_t *iris = eye->face->iris;
    if(!eye->variant) {
        memcpy(eye->pixels, iris->data, sizeof(s_saver_pixels));
        return;
    }
    const uint16_t *profile = (const uint16_t *)iris->data;
    for(int y = 0; y < eye->height; ++y) {
        uint16_t color = profile[y * iris->header.h / eye->height];
        uint16_t *row = eye->pixels + y * eye->width;
        for(int x = 0; x < eye->width; ++x) row[x] = color;
    }
}

static void paint_lids(const eye_t *eye, const pose_t *pose)
{
    const uint16_t lid = lv_color_to_u16(lv_color_hex(eye->variant ? 0xFFFFFF : 0x393F44));
    const uint16_t rim = lv_color_to_u16(lv_color_hex(0x042433));
    for(int x = 0; x < eye->width; ++x) {
        int upper = pose->upper_left + (pose->upper_right - pose->upper_left) * x / (eye->width - 1);
        int lower = pose->lower_left + (pose->lower_right - pose->lower_left) * x / (eye->width - 1);
        upper = upper * eye->height * 256 / POSE_SCALE;
        lower = lower * eye->height * 256 / POSE_SCALE;
        int top = upper / 256, bottom = lower / 256;
        for(int y = 0; y < top; ++y) eye->pixels[y * eye->width + x] = lid;
        for(int y = bottom + 1; y < eye->height; ++y) eye->pixels[y * eye->width + x] = lid;
        uint16_t *upper_edge = &eye->pixels[top * eye->width + x];
        uint16_t *lower_edge = &eye->pixels[bottom * eye->width + x];
        if(eye->variant) {
            uint16_t *under_upper = upper_edge + eye->width;
            uint16_t *above_lower = lower_edge - eye->width;
            *under_upper = lv_color_16_16_mix(rim, *under_upper, upper % 256);
            *above_lower = lv_color_16_16_mix(rim, *above_lower, 255 - lower % 256);
            *upper_edge = rim;
            *lower_edge = rim;
        }
        *upper_edge = lv_color_16_16_mix(lid, *upper_edge, upper % 256);
        *lower_edge = lv_color_16_16_mix(lid, *lower_edge, 255 - lower % 256);
    }
}

static void draw_eye(const eye_t *eye, const pose_t *pose)
{
    paint_iris(eye);
    draw_lens(eye, pose);
    paint_lids(eye, pose);

    if(eye->variant) {
        lv_area_t area;
        visor_area(eye->view, &area);
        lv_obj_invalidate_area(eye->view, &area);
    }
    else {
        lv_obj_invalidate(eye->view);
    }
}

static int lerp(int start, int end, int progress)
{
    return start + (end - start) * progress / ANIMATION_MAX;
}

static void animate_eye(void *data, int32_t value)
{
    eye_t *eye = data;
    const pose_t a = eye->from, b = eye->to;
    pose_t pose = {
        lerp(a.x, b.x, value), lerp(a.y, b.y, value),
        lerp(a.upper_left, b.upper_left, value), lerp(a.upper_right, b.upper_right, value),
        lerp(a.lower_left, b.lower_left, value), lerp(a.lower_right, b.lower_right, value),
    };
    if(!memcmp(&pose, &eye->drawn, sizeof(pose))) return;
    eye->drawn = pose;
    if(eye->variant) eye->drawn_core = eye->view;
    draw_eye(eye, &pose);
}

static pose_t target_pose(const eye_t *eye, unsigned target)
{
    pose_t pose = s_poses[target];
    if(!eye->variant) {
        pose.upper_left += eye->upper_offset;
        pose.upper_right += eye->upper_offset;
        return pose;
    }

    const pose_t rest = eye->variant->rest;
    const pose_t center = s_poses[0];
    int motion = eye->variant->motion;
    return (pose_t){
        rest.x + (pose.x - center.x) * motion / 100,
        rest.y + (pose.y - center.y) * motion / 100,
        rest.upper_left + (pose.upper_left - center.upper_left) * motion / 100,
        rest.upper_right + (pose.upper_right - center.upper_right) * motion / 100,
        rest.lower_left + (pose.lower_left - center.lower_left) * motion / 100,
        rest.lower_right + (pose.lower_right - center.lower_right) * motion / 100,
    };
}

static void look(eye_t *eye, unsigned target, bool move)
{
    eye->target = target;
    eye->to = target_pose(eye, target);
    lv_anim_delete(eye, animate_eye);
    if(!move) {
        eye->drawn = eye->to;
        draw_eye(eye, &eye->drawn);
        return;
    }

    eye->from = eye->drawn;
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, eye);
    lv_anim_set_exec_cb(&anim, animate_eye);
    lv_anim_set_values(&anim, 0, ANIMATION_MAX);
    lv_anim_set_duration(&anim, MOVE_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_start(&anim);
}

static void schedule_first_look(eye_t *eye)
{
    eye->target = 0;
    eye->look_started = lv_tick_get();
    eye->look_delay = FIRST_LOOK_MS;
}

static void tick_eye(eye_t *eye)
{
    if(lv_tick_elaps(eye->look_started) < eye->look_delay) return;
    unsigned next = random_between(0, sizeof(s_poses) / sizeof(s_poses[0]) - 2);
    if(next >= eye->target) ++next;
    look(eye, next, true);
    eye->look_started = lv_tick_get();
    eye->look_delay = random_between(MIN_LOOK_MS, MAX_LOOK_MS);
}

static bool allocate_visor(eye_t *eye, int width, int height)
{
    if(eye->pixels) return true;
    uint32_t size = width * height * 3;
    eye->pixels = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(!eye->pixels) return false;
    eye->width = width;
    eye->height = height;
    eye->visor.header.magic = LV_IMAGE_HEADER_MAGIC;
    eye->visor.header.cf = LV_COLOR_FORMAT_RGB565A8;
    eye->visor.header.w = width;
    eye->visor.header.h = height;
    eye->visor.header.stride = width * 2;
    eye->visor.data = (const uint8_t *)eye->pixels;
    eye->visor.data_size = size;
    uint8_t *alpha = (uint8_t *)(eye->pixels + width * height);
    /* The circular cutout ends just inside the bevel in the original artwork. */
    int inner = (width - 4) * (width - 4);
    int outer = (width - 2) * (width - 2);
    for(int y = 0; y < height; ++y) {
        int dy = 2 * y + 1 - height;
        for(int x = 0; x < width; ++x) {
            int dx = 2 * x + 1 - width;
            int distance = dx * dx + dy * dy;
            alpha[y * width + x] = LV_CLAMP(0, (outer - distance) * 255 / (outer - inner), 255);
        }
    }
    return true;
}

static eye_t *core_eye(lv_obj_t *core)
{
    return lv_image_get_src(core) == &portal_core_warning ? &s_warning : &s_normal;
}

static void core_draw(lv_event_t *event)
{
    lv_obj_t *core = lv_event_get_target_obj(event);
    eye_t *eye = core_eye(core);
    if(eye->drawn_core != core) return;
    lv_area_t area;
    visor_area(core, &area);
    lv_draw_image_dsc_t draw;
    lv_draw_image_dsc_init(&draw);
    lv_layer_t *layer = lv_event_get_layer(event);
    draw.base.layer = layer;
    lv_obj_init_draw_image_dsc(core, LV_PART_MAIN, &draw);
    draw.src = &eye->visor;
    lv_draw_image(layer, &draw, &area);
}

static void clear_core(lv_obj_t *core)
{
    eye_t *eye = core_eye(core);
    if(eye->drawn_core == core) eye->drawn_core = NULL;
    if(eye->view == core) {
        if(s_active_core == eye) portal_core_tick(NULL);
        eye->view = NULL;
    }
}

static void core_delete(lv_event_t *event)
{
    clear_core(lv_event_get_target_obj(event));
}

lv_obj_t *portal_core_create(lv_obj_t *parent, portal_core_variant_t variant, int x, int y)
{
    lv_obj_t *core = lv_image_create(parent);
    lv_obj_set_pos(core, x, y);
    lv_obj_remove_flag(core, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(core, core_draw, LV_EVENT_DRAW_MAIN_END, NULL);
    lv_obj_add_event_cb(core, core_delete, LV_EVENT_DELETE, NULL);
    portal_core_set_variant(core, variant);
    return core;
}

void portal_core_set_variant(lv_obj_t *core, portal_core_variant_t variant)
{
    const variant_t *style = &s_variants[variant];
    if(lv_image_get_src(core) == style->shell) return;
    clear_core(core);
    lv_obj_set_user_data(core, (void *)style);
    lv_image_set_src(core, style->shell);
}

void portal_core_tick(lv_obj_t *visible_core)
{
    if(visible_core && !lv_obj_is_visible(visible_core)) visible_core = NULL;
    if(s_active_core && s_active_core->view != visible_core) {
        lv_anim_delete(s_active_core, animate_eye);
        s_active_core = NULL;
    }
    if(!visible_core) return;

    if(!s_active_core) {
        eye_t *eye = core_eye(visible_core);
        bool warning = eye == &s_warning;
        if(!allocate_visor(eye, warning ? WARNING_W : CORE_W, warning ? WARNING_H : CORE_H)) return;
        s_active_core = eye;
        eye->view = visible_core;
        eye->variant = lv_obj_get_user_data(visible_core);
        eye->face = &eye->variant->face;
        if(eye->drawn_core != visible_core) {
            eye->drawn_core = NULL;
            eye->drawn = eye->variant->rest;
        }
        schedule_first_look(eye);
    }
    tick_eye(s_active_core);
}

static lv_obj_t *s_saver_shell;

lv_obj_t *portal_core_saver_create(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *visor = lv_canvas_create(parent);
    lv_obj_remove_flag(visor, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(visor, x + 61, y + 85);
    lv_canvas_set_buffer(visor, s_saver_pixels, SAVER_W, SAVER_H, LV_COLOR_FORMAT_RGB565);
    s_saver.view = visor;
    s_saver.pixels = s_saver_pixels;
    s_saver.width = SAVER_W;
    s_saver.height = SAVER_H;
    lv_obj_t *shell = lv_image_create(parent);
    lv_obj_remove_flag(shell, LV_OBJ_FLAG_CLICKABLE);
    s_saver_shell = shell;
    lv_obj_set_pos(shell, x, y);
    lv_image_set_src(shell, &portal_saver_shell);
    return shell;
}

void portal_core_saver_set_visible(bool visible)
{
    if (visible) {
        lv_obj_remove_flag(s_saver.view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_saver_shell, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_saver.view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_saver_shell, LV_OBJ_FLAG_HIDDEN);
    }
}

void portal_core_saver_set_face(portal_saver_face_t face, int lid, bool first)
{
    s_saver.face = &s_saver_faces[face];
    s_saver.upper_offset = lid;
    if(first) schedule_first_look(&s_saver);
    look(&s_saver, s_saver.target, !first);
    if(!first) draw_eye(&s_saver, &s_saver.drawn);
}

void portal_core_saver_tick(void)
{
    tick_eye(&s_saver);
}

void portal_core_saver_stop(void)
{
    lv_anim_delete(&s_saver, animate_eye);
}
