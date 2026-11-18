#ifndef PORTAL_UI_HOST_TEST
#include "portal_core_clip.h"
#include "board_sd.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "src/libs/lodepng/lodepng.h"
#include "src/libs/lz4/lz4.h"
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum {
    DISPLAY_SIZE = 300, FRAME_COUNT = 72, LEGACY_FRAME_COUNT = 144, FRAME_RATE = 12,
    CLIP_US = FRAME_COUNT * 1000000 / FRAME_RATE, MAX_PNG_BYTES = 1024 * 1024,
    PACKAGE_ALIGNMENT = 512, PACKAGE_HEADER_BYTES = 16,
    RGB565A8_FRAME_BYTES = DISPLAY_SIZE * DISPLAY_SIZE * 3,
    FRAMES_PER_BLOCK = 8, STORAGE_BLOCKS = FRAME_COUNT / FRAMES_PER_BLOCK,
    START_FRAMES = 12, FRAME_US = 1000000 / FRAME_RATE,
};
_Static_assert(FRAME_COUNT % FRAMES_PER_BLOCK == 0, "Clip storage requires whole blocks");
enum { CLIP_IDLE, CLIP_LOADING, CLIP_READY, CLIP_FAILED, CLIP_CANCELLED };
typedef enum { CLIP_FILES, CLIP_PNG_PACK, CLIP_RGB565A8, CLIP_LZ4, CLIP_FORMAT_COUNT } clip_format_t;
static const char *const s_formats[CLIP_FORMAT_COUNT] = {"files", "png", "rgb565a8", "lz4"};

static const char *const s_kind_names[PORTAL_CLIP_COUNT] = {
    [PORTAL_CLIP_NONE] = "none",
    [PORTAL_CLIP_OFFLINE] = "fallando",
    [PORTAL_CLIP_COMPLETE] = "feliz",
    [PORTAL_CLIP_ERROR] = "indignado",
    [PORTAL_CLIP_PAUSED] = "pausa",
    [PORTAL_CLIP_PRINTING] = "imprimiendo",
    [PORTAL_CLIP_STANDBY] = "pensativo",
};

static const char *const s_asset_roots[] = {
    BOARD_SD_MOUNT_PATH "/animaciones",
    BOARD_SD_MOUNT_PATH "/animaciones/codigo",
    BOARD_SD_MOUNT_PATH,
};

typedef struct {
    unsigned number;
    char name[sizeof(((struct dirent *)0)->d_name)];
} clip_frame_file_t;

typedef struct {
    char folder[64];
    clip_frame_file_t frames[LEGACY_FRAME_COUNT];
    unsigned count;
    bool legacy;
} clip_files_t;

typedef struct {
    int64_t sd_read_us;
    int64_t png_decode_us;
    int64_t lz4_decode_us;
    int64_t psram_copy_us;
    size_t png_bytes;
    size_t input_bytes;
} clip_load_timings_t;

typedef struct {
    uint8_t *blocks[STORAGE_BLOCKS];
    size_t slot_size;
} clip_storage_t;

static const char *TAG = "core_clip";
static lv_image_dsc_t s_frames[FRAME_COUNT];
static clip_storage_t s_storage;
static atomic_uint s_source_count, s_available_frames;
static atomic_int s_state;
// A format change and a printer state change share one selection for the worker.
static atomic_uint s_selected_clip, s_loaded_clip;
static atomic_int s_requested_format = CLIP_LZ4;
static _Atomic(TaskHandle_t) s_loader;
static bool s_loader_started; // Owned by the network console task.
static lv_obj_t *s_active_image;
static atomic_int s_active_view;
static atomic_uint s_darken;
static atomic_uint s_updates, s_loops, s_skipped;
static atomic_uint s_buffer_stalls, s_buffer_wait_us, s_late_frames, s_max_gap_us, s_loop_us;
static int64_t s_started_us;
static uint64_t s_previous_step;
static int64_t s_load_started_us;
static bool s_first_frame_pending, s_first_frame_presented;
static portal_core_clip_kind_t s_fallback_kind;
static bool s_frame_pending;
static unsigned s_frame_index;
static int64_t s_next_frame_us, s_last_frame_us, s_loop_started_us, s_buffer_started_us;

static bool has_benchmark_variants(portal_core_clip_kind_t kind)
{
    return kind == PORTAL_CLIP_PRINTING || kind == PORTAL_CLIP_STANDBY;
}

static size_t package_aligned_size(size_t size)
{
    return (size + PACKAGE_ALIGNMENT - 1) & ~(size_t)(PACKAGE_ALIGNMENT - 1);
}

static bool allocate_storage(clip_storage_t *storage, size_t slot_size)
{
    // A large free heap can still be fragmented by LVGL and print metadata.
    storage->slot_size = slot_size;
    for (unsigned i = 0; i < STORAGE_BLOCKS; ++i) {
        storage->blocks[i] = heap_caps_aligned_alloc(64, slot_size * FRAMES_PER_BLOCK,
                                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!storage->blocks[i]) return false;
    }
    return true;
}

static uint8_t *storage_frame(const clip_storage_t *storage, unsigned index)
{
    return storage->blocks[index / FRAMES_PER_BLOCK] + (index % FRAMES_PER_BLOCK) * storage->slot_size;
}

static void free_storage(clip_storage_t *storage)
{
    for (unsigned i = 0; i < STORAGE_BLOCKS; ++i) heap_caps_free(storage->blocks[i]);
    *storage = (clip_storage_t){0};
}

static int frame_number(const char *name, const char *label)
{
    const char *dot = strrchr(name, '.');
    if (!dot || strcasecmp(dot, ".png")) return -1;
    const char *digits = dot;
    while (digits > name && digits[-1] >= '0' && digits[-1] <= '9') --digits;
    if (digits == dot || dot - digits > 4) return -1;
    if (label) {
        size_t length = strlen(label);
        if ((size_t)(digits - name) < length || strncasecmp(name, label, length)) return -1;
        for (const char *p = name + length; p < digits; ++p) {
            if (*p != '_' && *p != '-' && *p != '.' && *p != ' ') return -1;
        }
    }
    int number = 0;
    for (const char *p = digits; p < dot; ++p) number = 10 * number + (*p - '0');
    return number;
}

static int compare_frames(const void *a, const void *b)
{
    const clip_frame_file_t *left = a, *right = b;
    return (int)left->number - (int)right->number;
}

static esp_err_t scan_folder(clip_files_t *files, const char *label, bool legacy)
{
    DIR *dir = opendir(files->folder);
    if (!dir) return errno == ENOENT || errno == ENOTDIR ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    unsigned count = 0;
    esp_err_t result = ESP_OK;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) {
            if (errno) result = ESP_FAIL;
            break;
        }
        if (entry->d_type != DT_REG) continue;
        int number = frame_number(entry->d_name, label);
        if (number < 0) continue;
        if (count == (legacy ? LEGACY_FRAME_COUNT : FRAME_COUNT)) {
            result = ESP_ERR_INVALID_SIZE;
            break;
        }
        clip_frame_file_t *frame = &files->frames[count++];
        frame->number = (unsigned)number;
        snprintf(frame->name, sizeof(frame->name), "%s", entry->d_name);
    }
    if (closedir(dir) != 0) return ESP_FAIL;
    if (result != ESP_OK) return result;
    if (!count) return ESP_ERR_NOT_FOUND;
    if (count != FRAME_COUNT && !(legacy && count == LEGACY_FRAME_COUNT))
        return ESP_ERR_INVALID_SIZE;
    qsort(files->frames, count, sizeof(files->frames[0]), compare_frames);
    for (unsigned i = 1; i < count; ++i) {
        if (files->frames[i].number != files->frames[0].number + i) return ESP_ERR_INVALID_SIZE;
    }
    files->count = count;
    files->legacy = legacy;
    return ESP_OK;
}

static esp_err_t find_frames(portal_core_clip_kind_t kind, clip_files_t *files)
{
    const char *label = s_kind_names[kind];
    for (unsigned i = 0; i < sizeof(s_asset_roots) / sizeof(*s_asset_roots); ++i) {
        snprintf(files->folder, sizeof(files->folder), "%s/%s", s_asset_roots[i], label);
        esp_err_t result = scan_folder(files, NULL, false);
        if (result != ESP_ERR_NOT_FOUND) return result;
        snprintf(files->folder, sizeof(files->folder), "%s", s_asset_roots[i]);
        result = scan_folder(files, label, false);
        if (result != ESP_ERR_NOT_FOUND) return result;
    }
    if (kind == PORTAL_CLIP_PRINTING) {
        snprintf(files->folder, sizeof(files->folder), "%s/codigo", s_asset_roots[0]);
        return scan_folder(files, "codigo", true);
    }
    if (kind == PORTAL_CLIP_STANDBY) {
        snprintf(files->folder, sizeof(files->folder), "%s/reposo", s_asset_roots[0]);
        return scan_folder(files, NULL, true);
    }
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t inspect_frame(const uint8_t *png, size_t size, bool legacy)
{
    unsigned width = 0, height = 0;
    LodePNGState header;
    lodepng_state_init(&header);
    unsigned error = lodepng_inspect(&width, &height, &header, png, size);
    lodepng_state_cleanup(&header);
    bool valid = legacy ? width && height && width <= DISPLAY_SIZE && height <= DISPLAY_SIZE
                        : width == DISPLAY_SIZE && height == DISPLAY_SIZE;
    return !error && valid ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t decode_png(const uint8_t *png, size_t size, bool legacy, lv_draw_buf_t **out,
                            clip_load_timings_t *timings)
{
    int64_t started = esp_timer_get_time();
    esp_err_t result = inspect_frame(png, size, legacy);
    if (result != ESP_OK) return result;

    // LVGL's bundled LodePNG returns a draw buffer containing straight RGBA8.
    unsigned char *decoded = NULL;
    unsigned width, height;
    unsigned error = lodepng_decode32(&decoded, &width, &height, png, size);
    timings->png_decode_us += esp_timer_get_time() - started;
    lv_draw_buf_t *draw = (lv_draw_buf_t *)decoded;
    if (error || !draw) {
        if (draw) lv_draw_buf_destroy(draw);
        ESP_LOGW(TAG, "PNG error=%u", error);
        return ESP_ERR_INVALID_RESPONSE;
    }
    *out = draw;
    return ESP_OK;
}

static esp_err_t decode_frame(const char *path, bool legacy, lv_draw_buf_t **out,
                              clip_load_timings_t *timings)
{
    int64_t started = esp_timer_get_time();
    FILE *file = fopen(path, "rb");
    if (!file) return ESP_ERR_NOT_FOUND;
    long size = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
    if (size < 33 || size > MAX_PNG_BYTES || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }
    timings->sd_read_us += esp_timer_get_time() - started;
    uint8_t *png = heap_caps_malloc((size_t)size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!png) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }
    started = esp_timer_get_time();
    size_t read = fread(png, 1, (size_t)size, file);
    fclose(file);
    timings->sd_read_us += esp_timer_get_time() - started;
    esp_err_t result = ESP_FAIL;
    if (read == (size_t)size) {
        timings->png_bytes += read;
        timings->input_bytes += read;
        result = decode_png(png, read, legacy, out, timings);
    }
    heap_caps_free(png);
    return result;
}

static void store_png_frame(unsigned index, const lv_draw_buf_t *draw, uint8_t *target,
                            clip_load_timings_t *timings)
{
    int64_t started = esp_timer_get_time();
    const unsigned stride = draw->header.w * 4;
    for (unsigned y = 0; y < draw->header.h; ++y) {
        const uint8_t *row = draw->data + y * draw->header.stride;
        uint8_t *dest = target + y * stride;
        for (unsigned i = 0; i < stride; i += 4) {
            dest[i] = row[i + 2];
            dest[i + 1] = row[i + 1];
            dest[i + 2] = row[i];
            dest[i + 3] = row[i + 3];
        }
    }
    s_frames[index] = (lv_image_dsc_t){
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_ARGB8888,
                   .w = draw->header.w, .h = draw->header.h, .stride = stride},
        .data_size = stride * draw->header.h, .data = target,
    };
    timings->psram_copy_us += esp_timer_get_time() - started;
}

static esp_err_t load_package(unsigned selection, portal_core_clip_kind_t kind, clip_format_t format,
                              clip_storage_t *storage, clip_load_timings_t *timings)
{
    char path[96];
    snprintf(path, sizeof(path), BOARD_SD_MOUNT_PATH "/animaciones/prueba_core/%s/%s.pcore",
             s_formats[format], s_kind_names[kind]);
    const uint8_t expected[] = {'P','C','O','R','E','1','\r','\n',
        DISPLAY_SIZE & 255, DISPLAY_SIZE >> 8, DISPLAY_SIZE & 255, DISPLAY_SIZE >> 8,
        FRAME_COUNT, 0, FRAME_RATE, format};
    // The ESP32-P4 and the package length table both use little endian.
    uint32_t header[PACKAGE_ALIGNMENT / sizeof(uint32_t)] __attribute__((aligned(64)));
    _Static_assert(PACKAGE_HEADER_BYTES + FRAME_COUNT * sizeof(uint32_t) <= sizeof(header),
                   "Package length table must fit in the first sector");
    int64_t started = esp_timer_get_time();
    FILE *file = fopen(path, "rb");
    if (!file) return ESP_ERR_NOT_FOUND;
    size_t read = fread(header, 1, sizeof(header), file);
    timings->sd_read_us += esp_timer_get_time() - started;
    timings->input_bytes += read;

    esp_err_t result = read == sizeof(header) && memcmp(header, expected, sizeof(expected)) == 0
                       ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
    const uint32_t *sizes = header + PACKAGE_HEADER_BYTES / sizeof(uint32_t);
    size_t input_capacity = 0;
    for (unsigned i = 0; result == ESP_OK && i < FRAME_COUNT; ++i) {
        size_t size = sizes[i];
        bool valid = false;
        switch (format) {
        case CLIP_PNG_PACK: valid = size >= 33 && size <= MAX_PNG_BYTES; break;
        case CLIP_RGB565A8: valid = size == RGB565A8_FRAME_BYTES; break;
        case CLIP_LZ4: valid = size > 0 && size <= LZ4_COMPRESSBOUND(RGB565A8_FRAME_BYTES); break;
        default: break;
        }
        if (!valid) result = ESP_ERR_INVALID_SIZE;
        if (valid && size > input_capacity) input_capacity = size;
    }

    const bool direct = format == CLIP_RGB565A8;
    size_t slot_size = format == CLIP_PNG_PACK ? DISPLAY_SIZE * DISPLAY_SIZE * 4 : RGB565A8_FRAME_BYTES;
    if (direct) slot_size = package_aligned_size(slot_size);
    uint8_t *input = NULL;
    if (result == ESP_OK) {
        if (!allocate_storage(storage, slot_size)) result = ESP_ERR_NO_MEM;
        if (result == ESP_OK && !direct) {
            input = heap_caps_aligned_alloc(64, package_aligned_size(input_capacity),
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!input) result = ESP_ERR_NO_MEM;
        }
    }

    for (unsigned i = 0; result == ESP_OK && i < FRAME_COUNT; ++i) {
        if (atomic_load(&s_selected_clip) != selection) {
            result = ESP_ERR_INVALID_STATE;
            break;
        }
        uint8_t *target = storage_frame(storage, i);
        size_t stored_size = package_aligned_size(sizes[i]);
        started = esp_timer_get_time();
        read = fread(direct ? target : input, 1, stored_size, file);
        timings->sd_read_us += esp_timer_get_time() - started;
        if (read != stored_size) {
            result = ESP_FAIL;
            break;
        }
        timings->input_bytes += read;

        if (format == CLIP_PNG_PACK) {
            lv_draw_buf_t *draw = NULL;
            timings->png_bytes += sizes[i];
            result = decode_png(input, sizes[i], false, &draw, timings);
            if (result == ESP_OK) {
                store_png_frame(i, draw, target, timings);
                lv_draw_buf_destroy(draw);
            }
        } else {
            if (format == CLIP_LZ4) {
                started = esp_timer_get_time();
                int decoded = LZ4_decompress_safe((const char *)input, (char *)target,
                                                   (int)sizes[i], RGB565A8_FRAME_BYTES);
                timings->lz4_decode_us += esp_timer_get_time() - started;
                if (decoded != RGB565A8_FRAME_BYTES) {
                    result = ESP_ERR_INVALID_RESPONSE;
                    break;
                }
            }
            s_frames[i] = (lv_image_dsc_t){
                .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565A8,
                           .w = DISPLAY_SIZE, .h = DISPLAY_SIZE, .stride = DISPLAY_SIZE * 2},
                .data_size = RGB565A8_FRAME_BYTES, .data = target,
            };
            // Publish complete, immutable frames. The GUI can read this prefix while we load.
            if (format == CLIP_LZ4) atomic_store(&s_available_frames, i + 1);
        }
    }

    started = esp_timer_get_time();
    if (result == ESP_OK && (fgetc(file) != EOF || ferror(file))) result = ESP_ERR_INVALID_SIZE;
    if (fclose(file) != 0 && result == ESP_OK) result = ESP_FAIL;
    timings->sd_read_us += esp_timer_get_time() - started;
    heap_caps_free(input);
    return result;
}

static void load_clip(unsigned selection)
{
    portal_core_clip_kind_t kind = selection % PORTAL_CLIP_COUNT;
    clip_format_t format = selection / PORTAL_CLIP_COUNT;
    clip_load_timings_t timings = {0};
    int64_t began = esp_timer_get_time();
    s_load_started_us = began;
    esp_err_t result = board_sd_mount();
    if ((result == ESP_ERR_INVALID_STATE || result == ESP_ERR_TIMEOUT) &&
        atomic_load(&s_selected_clip) == selection) {
        vTaskDelay(pdMS_TO_TICKS(150));
        result = board_sd_mount();
    }
    unsigned sources = 0;
    bool cancelled = false;
    bool mounted = result == ESP_OK;
    if (mounted && format != CLIP_FILES) {
        result = load_package(selection, kind, format, &s_storage, &timings);
        cancelled = result == ESP_ERR_INVALID_STATE;
        if (result == ESP_OK) sources = FRAME_COUNT;
    } else if (mounted) {
        clip_files_t *files = heap_caps_malloc(sizeof(*files), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        result = files ? find_frames(kind, files) : ESP_ERR_NO_MEM;
        if (result == ESP_OK) sources = files->count;
        unsigned step = sources / FRAME_COUNT;
        for (unsigned i = 0; result == ESP_OK && i < FRAME_COUNT; ++i) {
            if (atomic_load(&s_selected_clip) != selection) {
                cancelled = true;
                break;
            }
            char path[sizeof(files->folder) + sizeof(files->frames[0].name)];
            snprintf(path, sizeof(path), "%s/%s", files->folder, files->frames[i * step].name);
            lv_draw_buf_t *draw = NULL;
            result = decode_frame(path, files->legacy, &draw, &timings);
            if (result == ESP_OK && i == 0) {
                size_t bytes_per_frame = (size_t)draw->header.w * draw->header.h * 4;
                if (!allocate_storage(&s_storage, bytes_per_frame)) result = ESP_ERR_NO_MEM;
            }
            if (result == ESP_OK && i && (draw->header.w != s_frames[0].header.w ||
                                         draw->header.h != s_frames[0].header.h)) result = ESP_ERR_INVALID_SIZE;
            if (result == ESP_OK) store_png_frame(i, draw, storage_frame(&s_storage, i), &timings);
            if (draw) lv_draw_buf_destroy(draw);
            if (result != ESP_OK) ESP_LOGW(TAG, "%s: %s", path, esp_err_to_name(result));
        }
        heap_caps_free(files);
    }
    if (mounted) {
        esp_err_t unmount = board_sd_unmount();
        if (result == ESP_OK) result = unmount;
    }
    if (cancelled || atomic_load(&s_selected_clip) != selection) {
        unsigned available = atomic_load(&s_available_frames);
        atomic_store(&s_state, CLIP_CANCELLED);
        ESP_LOGI(TAG, "CORE_CLIP_CANCELLED kind=%s format=%s frames=%u",
                 s_kind_names[kind], s_formats[format], available);
    } else if (result == ESP_OK) {
        size_t storage_bytes = s_storage.slot_size * FRAME_COUNT;
        atomic_store(&s_source_count, sources);
        int64_t total_us = esp_timer_get_time() - began;
        atomic_store(&s_available_frames, FRAME_COUNT);
        atomic_store(&s_state, CLIP_READY);
        int64_t other_us = total_us - timings.sd_read_us - timings.png_decode_us -
                           timings.psram_copy_us - timings.lz4_decode_us;
        ESP_LOGI(TAG, "CORE_CLIP_READY kind=%s source=%u frames=%u psram_bytes=%zu load_ms=%" PRId64
                      " sd_read_us=%" PRId64 " png_decode_us=%" PRId64 " psram_copy_us=%" PRId64
                      " other_us=%" PRId64 " png_bytes=%zu format=%s lz4_decode_us=%" PRId64 " input_bytes=%zu",
                 s_kind_names[kind], sources, FRAME_COUNT, storage_bytes,
                 total_us / 1000, timings.sd_read_us, timings.png_decode_us, timings.psram_copy_us,
                 other_us, timings.png_bytes, s_formats[format], timings.lz4_decode_us, timings.input_bytes);
    } else {
        if (!atomic_load(&s_available_frames)) free_storage(&s_storage);
        atomic_store(&s_state, CLIP_FAILED);
        ESP_LOGW(TAG, "CORE_CLIP_FALLBACK kind=%s error=%s format=%s",
                 s_kind_names[kind], esp_err_to_name(result), s_formats[format]);
    }
}

static void clip_worker(void *argument)
{
    (void)argument;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        unsigned selection = atomic_load(&s_selected_clip);
        if (selection == PORTAL_CLIP_NONE || atomic_load(&s_state) != CLIP_IDLE) continue;
        atomic_store(&s_loaded_clip, selection);
        atomic_store(&s_state, CLIP_LOADING);
        load_clip(selection);
    }
}

void portal_core_clip_start_load(void)
{
    if (s_loader_started) return;
    s_loader_started = true;
    TaskHandle_t worker = NULL;
    // The display and LVGL run on CPU0. Keep PNG and LZ4 work off that CPU.
    if (xTaskCreatePinnedToCore(clip_worker, "core_clip", 6144, NULL, 2, &worker, 1) == pdPASS)
        atomic_store(&s_loader, worker);
    else ESP_LOGW(TAG, "CORE_CLIP_FALLBACK no se pudo iniciar la carga");
}

portal_core_clip_kind_t portal_core_clip_for_state(klipper_state_t state)
{
    switch (state) {
    case KLIPPER_OFFLINE: return PORTAL_CLIP_OFFLINE;
    case KLIPPER_COMPLETE: return PORTAL_CLIP_COMPLETE;
    case KLIPPER_ERROR:
    case KLIPPER_CANCELLED: return PORTAL_CLIP_ERROR;
    case KLIPPER_PAUSED: return PORTAL_CLIP_PAUSED;
    case KLIPPER_PRINTING: return PORTAL_CLIP_PRINTING;
    case KLIPPER_STANDBY: return PORTAL_CLIP_STANDBY;
    default: return PORTAL_CLIP_NONE;
    }
}

static void detach_image(void)
{
    s_first_frame_pending = false;
    s_frame_pending = false;
    if (!s_active_image) return;
    lv_obj_add_flag(s_active_image, LV_OBJ_FLAG_HIDDEN);
    lv_image_set_src(s_active_image, NULL);
    s_active_image = NULL;
    atomic_store(&s_active_view, PORTAL_CLIP_HIDDEN);
    atomic_store(&s_darken, 0);
}

void portal_core_clip_select(portal_core_clip_kind_t kind)
{
    unsigned format = atomic_load(&s_requested_format);
    // The PNG and raw comparison packages exist only for these two clips.
    if (kind == PORTAL_CLIP_NONE || (format != CLIP_LZ4 && !has_benchmark_variants(kind)))
        format = CLIP_FILES;
    unsigned selection = kind + format * PORTAL_CLIP_COUNT;
    int state = atomic_load(&s_state);
    unsigned loaded = atomic_load(&s_loaded_clip);
    if (kind != s_fallback_kind || format != CLIP_LZ4) s_fallback_kind = PORTAL_CLIP_NONE;
    if (format == CLIP_LZ4 && state == CLIP_FAILED && loaded == selection) s_fallback_kind = kind;
    if (s_fallback_kind == kind) selection = kind;
    atomic_store(&s_selected_clip, selection);

    if (state == CLIP_CANCELLED || (state == CLIP_FAILED && loaded != selection) ||
        (state == CLIP_READY && kind != PORTAL_CLIP_NONE && loaded != selection)) {
        // The worker has stopped. Only the GUI releases storage, after removing LVGL references.
        detach_image();
        unsigned available = atomic_exchange(&s_available_frames, 0);
        for (unsigned i = 0; i < available; ++i) lv_image_cache_drop(&s_frames[i]);
        free_storage(&s_storage);
        s_first_frame_presented = false;
        atomic_store(&s_state, CLIP_IDLE);
        state = CLIP_IDLE;
    }
    if (kind == PORTAL_CLIP_NONE) return;
    TaskHandle_t worker = atomic_load(&s_loader);
    if (worker && state == CLIP_IDLE) xTaskNotifyGive(worker);
}

lv_obj_t *portal_core_clip_create(lv_obj_t *parent, lv_obj_t *original)
{
    lv_obj_t *image = lv_image_create(parent);
    lv_obj_set_size(image, DISPLAY_SIZE, DISPLAY_SIZE);
    lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_align_to(image, original, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
    return image;
}

static void queue_stream_frame(lv_obj_t *image, unsigned index)
{
    lv_image_set_src(image, &s_frames[index]);
    s_frame_index = index;
    s_frame_pending = true;
    lv_timer_ready(lv_display_get_refr_timer(lv_obj_get_display(image)));
}

static void start_stream(lv_obj_t *image)
{
    s_next_frame_us = 0;
    s_last_frame_us = 0;
    s_buffer_started_us = 0;
    atomic_store(&s_buffer_stalls, 0);
    atomic_store(&s_buffer_wait_us, 0);
    atomic_store(&s_late_frames, 0);
    atomic_store(&s_max_gap_us, 0);
    atomic_store(&s_loop_us, 0);
    queue_stream_frame(image, 0);
}

static void tick_stream(lv_obj_t *image, unsigned available, int64_t now_us)
{
    if (s_frame_pending || now_us < s_next_frame_us) return;
    unsigned next = (s_frame_index + 1) % FRAME_COUNT;
    if (next >= available) {
        if (!s_buffer_started_us) {
            s_buffer_started_us = now_us;
            atomic_fetch_add(&s_buffer_stalls, 1);
        }
        return;
    }
    // Show every frame in order, even if the previous display refresh ran late.
    s_next_frame_us += FRAME_US;
    if (s_buffer_started_us) {
        atomic_fetch_add(&s_buffer_wait_us, (unsigned)(now_us - s_buffer_started_us));
        s_buffer_started_us = 0;
        s_next_frame_us = now_us + FRAME_US;
    } else if (s_next_frame_us <= now_us) {
        s_next_frame_us = now_us + FRAME_US;
    }
    queue_stream_frame(image, next);
}

bool portal_core_clip_update(lv_obj_t *image, portal_core_clip_view_t view,
                             portal_core_clip_kind_t kind, int64_t now_us)
{
    unsigned selection = atomic_load(&s_selected_clip);
    bool stream = selection / PORTAL_CLIP_COUNT == CLIP_LZ4;
    int state = atomic_load(&s_state);
    unsigned available = atomic_load(&s_available_frames);
    bool playable = state == CLIP_READY || (stream && state == CLIP_LOADING && available >= START_FRAMES);
    if (kind == PORTAL_CLIP_NONE || !playable ||
        selection % PORTAL_CLIP_COUNT != kind || atomic_load(&s_loaded_clip) != selection)
        image = NULL;
    bool changed = image != s_active_image;
    if (changed) {
        detach_image();
        if (image) {
            if (stream) {
                start_stream(image);
            } else {
                lv_image_set_src(image, &s_frames[0]);
                atomic_fetch_add(&s_updates, 1);
                s_started_us = now_us;
                s_previous_step = 0;
            }
            lv_obj_remove_flag(image, LV_OBJ_FLAG_HIDDEN);
            s_first_frame_pending = (stream || has_benchmark_variants(kind)) && !s_first_frame_presented;
        }
        s_active_image = image;
        atomic_store(&s_darken, image ? lv_obj_get_style_image_recolor_opa(image, LV_PART_MAIN) : 0);
        atomic_store(&s_active_view, image ? view : PORTAL_CLIP_HIDDEN);
    }
    if (!image || changed) return image != NULL;
    if (stream) {
        tick_stream(image, available, now_us);
        return true;
    }

    uint64_t step = (uint64_t)(now_us - s_started_us) * FRAME_COUNT / CLIP_US;
    if (step != s_previous_step) {
        lv_image_set_src(image, &s_frames[step % FRAME_COUNT]);
        atomic_fetch_add(&s_updates, 1);
        atomic_fetch_add(&s_loops, step / FRAME_COUNT - s_previous_step / FRAME_COUNT);
        atomic_fetch_add(&s_skipped, step - s_previous_step - 1);
        s_previous_step = step;
    }
    return true;
}

static void clip_frame_flushed(lv_event_t *event)
{
    // The panel's final flush waits for the framebuffer handoff before returning.
    if ((!s_frame_pending && !s_first_frame_pending) ||
        !lv_display_flush_is_last(lv_event_get_target(event))) return;
    int64_t now_us = esp_timer_get_time();
    if (s_frame_pending) {
        s_frame_pending = false;
        atomic_fetch_add(&s_updates, 1);
        if (!s_last_frame_us) {
            s_loop_started_us = now_us;
            s_next_frame_us = now_us + FRAME_US;
        } else {
            unsigned gap = (unsigned)(now_us - s_last_frame_us);
            if (gap > atomic_load(&s_max_gap_us)) atomic_store(&s_max_gap_us, gap);
            if (gap > 100000) atomic_fetch_add(&s_late_frames, 1);
            if (s_frame_index == 0) {
                atomic_fetch_add(&s_loops, 1);
                atomic_store(&s_loop_us, (unsigned)(now_us - s_loop_started_us));
                s_loop_started_us = now_us;
            }
        }
        s_last_frame_us = now_us;
    }
    if (!s_first_frame_pending) return;
    s_first_frame_pending = false;
    s_first_frame_presented = true;
    unsigned selection = atomic_load(&s_loaded_clip);
    ESP_LOGI(TAG, "CORE_CLIP_PRESENT kind=%s format=%s first_present_us=%" PRId64 " view=%s available=%u",
             s_kind_names[selection % PORTAL_CLIP_COUNT], s_formats[selection / PORTAL_CLIP_COUNT],
             now_us - s_load_started_us,
             atomic_load(&s_active_view) == PORTAL_CLIP_SAVER ? "saver" : "main",
             atomic_load(&s_available_frames));
}

void portal_core_clip_track_present(lv_display_t *display)
{
    lv_display_add_event_cb(display, clip_frame_flushed, LV_EVENT_FLUSH_FINISH, NULL);
}

void portal_core_clip_set_format(const char *name)
{
    for (int format = CLIP_FILES; format < CLIP_FORMAT_COUNT; ++format) {
        if (strcmp(name, s_formats[format]) != 0) continue;
        atomic_store(&s_requested_format, format);
        printf("CORE_FORMAT format=%s\n", name);
        return;
    }
    printf("Uso: core format files | png | rgb565a8 | lz4\n");
}

void portal_core_clip_print_status(void)
{
    static const char *const names[] = {"idle", "loading", "ready", "failed", "cancelled"};
    static const char *const views[] = {"hidden", "main", "saver"};
    int state = atomic_load(&s_state);
    int view = atomic_load(&s_active_view);
    unsigned selection = atomic_load(&s_loaded_clip);
    printf("CORE_CLIP state=%s frames=%u playing=%d updates=%u loops=%u skipped=%u source=%u box=%ux%u duration_ms=%u view=%s darken=%u kind=%s format=%s requested=%s"
           " buffer_start=%u stalls=%u wait_us=%u late_frames=%u max_gap_us=%u loop_us=%u\n",
           names[state], atomic_load(&s_available_frames), view != PORTAL_CLIP_HIDDEN,
           atomic_load(&s_updates), atomic_load(&s_loops), atomic_load(&s_skipped),
           state == CLIP_READY ? atomic_load(&s_source_count) : 0, DISPLAY_SIZE, DISPLAY_SIZE, CLIP_US / 1000,
           views[view], atomic_load(&s_darken) * 100 / LV_OPA_COVER,
           s_kind_names[selection % PORTAL_CLIP_COUNT], s_formats[selection / PORTAL_CLIP_COUNT],
           s_formats[atomic_load(&s_requested_format)], START_FRAMES, atomic_load(&s_buffer_stalls),
           atomic_load(&s_buffer_wait_us), atomic_load(&s_late_frames), atomic_load(&s_max_gap_us),
           atomic_load(&s_loop_us));
}

void portal_core_clip_scan_card(void)
{
    esp_err_t mount = board_sd_mount();
    if (mount != ESP_OK) {
        printf("CORE_SCAN mount=%s\n", esp_err_to_name(mount));
        return;
    }
    clip_files_t *files = heap_caps_malloc(sizeof(*files), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    unsigned passed = 0;
    for (unsigned i = PORTAL_CLIP_OFFLINE; files && i < PORTAL_CLIP_COUNT; ++i) {
        esp_err_t result = find_frames(i, files);
        if (result != ESP_OK) {
            printf("CORE_SCAN kind=%s result=%s\n", s_kind_names[i], esp_err_to_name(result));
            continue;
        }
        unsigned checked = 0;
        for (unsigned j = 0; j < files->count; ++j) {
            char path[sizeof(files->folder) + sizeof(files->frames[0].name)];
            snprintf(path, sizeof(path), "%s/%s", files->folder, files->frames[j].name);
            FILE *file = fopen(path, "rb");
            if (!file) {
                result = ESP_ERR_NOT_FOUND;
                break;
            }
            uint8_t header[33];
            size_t size = fread(header, 1, sizeof(header), file);
            fclose(file);
            result = inspect_frame(header, size, files->legacy);
            if (result != ESP_OK) break;
            ++checked;
        }
        if (result == ESP_OK) ++passed;
        printf("CORE_SCAN kind=%s result=%s png=%u checked=%u folder=%s first=%s last=%s\n",
               s_kind_names[i], esp_err_to_name(result), files->count, checked, files->folder,
               files->frames[0].name, files->frames[files->count - 1].name);
    }
    if (!files) printf("CORE_SCAN error=ESP_ERR_NO_MEM\n");
    heap_caps_free(files);
    esp_err_t unmount = board_sd_unmount();
    printf("CORE_SCAN_DONE passed=%u total=%u unmount=%s\n", passed, PORTAL_CLIP_COUNT - 1, esp_err_to_name(unmount));
}
#endif
