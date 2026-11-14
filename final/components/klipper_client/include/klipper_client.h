#pragma once
#include "esp_err.h"
#include "klipper_tuning.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  KLIPPER_FILES_MAX = 15,
  KLIPPER_MACROS_MAX = 20,
  KLIPPER_OBJECTS_MAX = 16,
  KLIPPER_THUMBNAIL_MAX = 128 * 1024,
  KLIPPER_TOOLS_MAX = 10
};
typedef enum {
  KLIPPER_STARTUP,
  KLIPPER_OFFLINE,
  KLIPPER_STANDBY,
  KLIPPER_PRINTING,
  KLIPPER_PAUSED,
  KLIPPER_COMPLETE,
  KLIPPER_CANCELLED,
  KLIPPER_ERROR
} klipper_state_t;
typedef enum {
  KLIPPER_PENDING,
  KLIPPER_COMPLETED,
  KLIPPER_REJECTED,
  KLIPPER_UNKNOWN
} klipper_command_status_t;
typedef struct {
  char name[128];
  bool excluded, active, bounds_valid;
  float min_x, max_x, min_y, max_y, center_x, center_y;
} klipper_object_t;
typedef struct {
  uint32_t revision, endpoint_generation, metadata_revision;
  uint32_t error_revision;
  bool socket_connected, available, initial_snapshot;
  klipper_state_t state, raw_state;
  bool pause_pending;
  uint32_t job_id;
  float hotend_temperature, hotend_target, bed_temperature, bed_target,
      progress;
  bool speed_factor_valid, flow_factor_valid, fan_speed_valid;
  float speed_percent, flow_percent, fan_percent;
  uint32_t print_duration, time_remaining;
  char filename[256], error[384], connection_detail[160]; // error is a preview.
  bool homed_x, homed_y, homed_z, bed_bounds_valid;
  float position_x, position_y, position_z, bed_min_x, bed_max_x, bed_min_y,
      bed_max_y;
  bool files_loading, macros_loading, objects_loading, metadata_loading;
  unsigned files_count, macros_count, objects_count;
  char files[KLIPPER_FILES_MAX][256];
  char macros[KLIPPER_MACROS_MAX][128];
  klipper_object_t objects[KLIPPER_OBJECTS_MAX];
} klipper_snapshot_t;
typedef struct {
  unsigned tool_index;
  bool has_color, has_weight, referenced;
  char color[8];
  float weight_grams;
} klipper_tool_metadata_t;
typedef struct {
  uint32_t revision, endpoint_generation;
  bool valid, has_estimated_time, has_weight, has_length;
  char filename[256];
  float estimated_seconds, weight_grams, filament_length_mm;
  unsigned colors_count;
  char colors[10][8];
  unsigned tools_count;
  klipper_tool_metadata_t tools[KLIPPER_TOOLS_MAX];
  uint16_t thumbnail_width, thumbnail_height;
  // Captured from one immutable metadata version; PNG bytes are copied after
  // releasing the backend lock. UI owns its copy and decoder lifetime.
  size_t thumbnail_size;
  uint8_t thumbnail_png[KLIPPER_THUMBNAIL_MAX];
} klipper_metadata_t;
typedef struct {
  bool valid;
  uint32_t revision, endpoint_generation, job_id;
  klipper_state_t result;
  char filename[256];
  uint32_t print_duration;
  float progress;
} klipper_last_job_t;
typedef struct {
  uint32_t id, generation, context;
  klipper_command_status_t status;
  char command[160], message[320];
} klipper_command_result_t;

typedef enum {
  K_ACTION_GCODE,
  K_ACTION_EMERGENCY,
  K_ACTION_SHUTDOWN,
  K_ACTION_PRINT_FILE,
  K_ACTION_MACRO,
  K_ACTION_EXCLUDE,
  // value is the decimal job_id of the confirmed terminal job.
  K_ACTION_RESET_JOB
} klipper_action_t;
// Bind an action to the endpoint which was displayed/confirmed by the UI.
bool klipper_client_action(klipper_action_t action, const char *value,
                           uint32_t expected_generation);
// Caller context is echoed in command results. Zero is the default.
bool klipper_client_send_gcode_at(const char *script, uint32_t expected_generation,
                                 uint32_t context);
// Applies one exact bounded percentage to the displayed active job. Results
// use the ordinary command queue; a missing acknowledgement is never retried.
bool klipper_client_set_print_tuning(klipper_tuning_t control, unsigned percent,
                                      uint32_t expected_generation,
                                      uint32_t expected_job_id);
bool klipper_client_set_print_tuning_with_context(klipper_tuning_t control, unsigned percent,
                                                 uint32_t expected_generation,
                                                 uint32_t expected_job_id, uint32_t context);

// All public calls are thread-safe; none calls LVGL or waits for HTTP
// completion.
esp_err_t klipper_client_start(void);
void klipper_client_snapshot(klipper_snapshot_t *out);
// Copies the full fault detail for these snapshot tokens. The caller owns the
// result and releases it with free(). NULL means no matching detail or no memory.
char *klipper_client_copy_error_detail(uint32_t expected_generation,
                                       uint32_t expected_error_revision);
bool klipper_client_metadata(klipper_metadata_t *out);
bool klipper_client_last_job(klipper_last_job_t *out);
bool klipper_client_last_job_metadata(klipper_metadata_t *out);
bool klipper_client_pop_result(klipper_command_result_t *out);
bool klipper_client_send_gcode(const char *script);
bool klipper_client_emergency_stop(void);
bool klipper_client_shutdown_host(void);
bool klipper_client_print_file(const char *filename);
bool klipper_client_run_macro(const char *name);
bool klipper_client_exclude_object(const char *name);
void klipper_client_fetch_files(void);
void klipper_client_fetch_macros(void);
void klipper_client_fetch_objects(void);
void klipper_client_fetch_metadata(const char *filename);

#ifdef __cplusplus
}
#endif
