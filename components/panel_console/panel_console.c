#include "panel_console.h"
#include "cJSON.h"
#include "device_network.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "klipper_client.h"
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "panel_console";
static atomic_bool busy = false;
enum {
  READ_FILES = 1,
  READ_MACROS,
  READ_OBJECTS,
  READ_METADATA,
  TEST_NORMAL,
  TEST_UNCERTAIN
};

static const char *state_name(klipper_state_t state) {
  static const char *names[] = {"startup", "offline",  "standby",   "printing",
                                "paused",  "complete", "cancelled", "error"};
  return state >= KLIPPER_STARTUP && state <= KLIPPER_ERROR ? names[state]
                                                            : "invalid";
}
static uint32_t png_u32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | p[3];
}
static void print_metadata(const klipper_metadata_t *m) {
  unsigned width = 0, height = 0;
  if (m->thumbnail_size >= 24) {
    width = png_u32(m->thumbnail_png + 16);
    height = png_u32(m->thumbnail_png + 20);
  }
  unsigned active = 0;
  for (unsigned i = 0; i < m->tools_count; ++i) {
    const klipper_tool_metadata_t *tool = &m->tools[i];
    bool visible = !tool->has_weight || tool->weight_grams > 0 || tool->referenced;
    if (visible) ++active;
    printf("PANEL_FILAMENT tool=%u color=%s weight=%s%.3fg referenced=%d visible=%d\n",
           tool->tool_index, tool->has_color ? tool->color : "unknown",
           tool->has_weight ? "" : "n/a ", (double)tool->weight_grams,
           tool->referenced, visible);
  }
  printf("PANEL_METADATA valid=%d gen=%" PRIu32 " revision=%" PRIu32
         " file=%s seconds=%.1f weight=%s%.2fg length=%s%.2fmm colors=%u "
         "tools=%u active=%u png=%u %ux%u\n",
         m->valid, m->endpoint_generation, m->revision, m->filename,
         (double)m->estimated_seconds, m->has_weight ? "" : "n/a ",
         (double)m->weight_grams, m->has_length ? "" : "n/a ",
         (double)m->filament_length_mm, m->colors_count, m->tools_count, active,
         (unsigned)m->thumbnail_size, width, height);
}
static void print_status(void) {
  klipper_snapshot_t *s = calloc(1, sizeof(*s));
  klipper_metadata_t *m = calloc(1, sizeof(*m));
  if (!s || !m) {
    printf("PANEL_ERROR no memory\n");
    free(s);
    free(m);
    return;
  }
  klipper_client_snapshot(s);
  klipper_client_metadata(m);
  printf("PANEL_STATUS state=%s available=%d ws=%d snapshot=%d gen=%" PRIu32
         " revision=%" PRIu32 " raw=%s pause_pending=%d job=%" PRIu32 "\n",
         state_name(s->state), s->available, s->socket_connected,
         s->initial_snapshot, s->endpoint_generation, s->revision,
         state_name(s->raw_state), s->pause_pending, s->job_id);
  printf("PANEL_TEMPERATURE hotend=%.1f/%.1f bed=%.1f/%.1f progress=%.1f%% "
         "duration=%" PRIu32 " remaining=%" PRIu32 " file=%s\n",
         (double)s->hotend_temperature, (double)s->hotend_target,
         (double)s->bed_temperature, (double)s->bed_target, (double)s->progress,
         s->print_duration, s->time_remaining, s->filename);
  printf("PANEL_TUNING speed_valid=%u speed=%.2f flow_valid=%u flow=%.2f "
         "fan_valid=%u fan=%.2f\n",
         (unsigned)s->speed_factor_valid, (double)s->speed_percent,
         (unsigned)s->flow_factor_valid, (double)s->flow_percent,
         (unsigned)s->fan_speed_valid, (double)s->fan_percent);
  printf("PANEL_LISTS files=%u loading=%d macros=%u loading=%d objects=%u "
         "loading=%d metadata_loading=%d detail=%s error=%s\n",
         s->files_count, s->files_loading, s->macros_count, s->macros_loading,
         s->objects_count, s->objects_loading, s->metadata_loading,
         s->connection_detail, s->error);
  print_metadata(m);
  free(m);
  free(s);
}
static bool same_endpoint(const device_network_snapshot_t *expected) {
  device_network_snapshot_t now = {0};
  device_network_snapshot(&now);
  return now.has_ip &&
         now.endpoint_generation == expected->endpoint_generation &&
         now.moonraker_port == expected->moonraker_port &&
         strcmp(now.moonraker_host, expected->moonraker_host) == 0;
}
static bool mock_identity(const device_network_snapshot_t *net) {
  if (!net->has_ip || !net->moonraker_host[0] || net->moonraker_port != 17125 ||
      !same_endpoint(net))
    return false;
  char url[180], body[1025] = {0};
  snprintf(url, sizeof(url), "http://%s:%u/__test/identity",
           net->moonraker_host, net->moonraker_port);
  esp_http_client_config_t cfg = {.url = url,
                                  .timeout_ms = 2000,
                                  .disable_auto_redirect = true,
                                  .buffer_size = 512};
  esp_http_client_handle_t http = esp_http_client_init(&cfg);
  if (!http)
    return false;
  bool ok = esp_http_client_open(http, 0) == ESP_OK;
  if (ok)
    ok = esp_http_client_fetch_headers(http) >= 0 &&
         esp_http_client_get_status_code(http) == 200;
  size_t received = 0;
  int64_t began = esp_timer_get_time();
  while (ok && received < sizeof(body) - 1 &&
         esp_timer_get_time() - began < 3000000) {
    int n = esp_http_client_read(http, body + received,
                                 sizeof(body) - 1 - received);
    if (n < 0) {
      ok = false;
      break;
    }
    if (!n)
      break;
    received += (size_t)n;
  }
  ok = ok && esp_http_client_is_complete_data_received(http) &&
       same_endpoint(net);
  esp_http_client_close(http);
  esp_http_client_cleanup(http);
  cJSON *doc = ok ? cJSON_ParseWithLength(body, received) : NULL;
  cJSON *service = cJSON_GetObjectItemCaseSensitive(doc, "service"),
        *test = cJSON_GetObjectItemCaseSensitive(doc, "test_only");
  ok = doc && cJSON_IsString(service) && service->valuestring &&
       strcmp(service->valuestring, "JC4880P443_MOONRAKER_SIMULATOR") == 0 &&
       cJSON_IsTrue(test);
  cJSON_Delete(doc);
  return ok;
}
static bool wait_lists(klipper_snapshot_t *s,
                       const device_network_snapshot_t *net, unsigned mode) {
  int64_t began = esp_timer_get_time();
  do {
    if (!same_endpoint(net))
      return false;
    klipper_client_snapshot(s);
    bool loading =
        mode == READ_FILES    ? s->files_loading
        : mode == READ_MACROS ? s->macros_loading
        : mode == READ_OBJECTS
            ? s->objects_loading
            : (s->files_loading || s->macros_loading || s->objects_loading);
    if (!loading)
      return true;
    vTaskDelay(pdMS_TO_TICKS(100));
  } while (esp_timer_get_time() - began < 15000000);
  return false;
}
static bool send_mock_action(klipper_action_t action, const char *value,
                             const device_network_snapshot_t *net) {
  if (!same_endpoint(net)) {
    ESP_LOGE(TAG, "PANEL_TEST_ABORT endpoint changed");
    return false;
  }
  bool accepted =
      klipper_client_action(action, value, net->endpoint_generation);
  ESP_LOGI(TAG, "PANEL_TEST_ACTION action=%d value=%s accepted=%d gen=%" PRIu32,
           action, value ? value : "", accepted, net->endpoint_generation);
  return accepted;
}
static void worker(void *arg) {
  unsigned mode = (unsigned)(uintptr_t)arg;
  klipper_snapshot_t *s = calloc(1, sizeof(*s));
  klipper_metadata_t *m = calloc(1, sizeof(*m));
  device_network_snapshot_t net = {0};
  device_network_snapshot(&net);
  if (!s || !m) {
    ESP_LOGE(TAG, "PANEL_ERROR no memory");
    goto done;
  }
  if (mode == TEST_NORMAL || mode == TEST_UNCERTAIN) {
    klipper_client_snapshot(s);
    if (!s->available || s->endpoint_generation != net.endpoint_generation ||
        !mock_identity(&net)) {
      ESP_LOGE(TAG, "PANEL_TEST_REFUSED require ready endpoint and positive "
                    "simulator identity on port 17125");
      goto done;
    }
    ESP_LOGI(TAG, "PANEL_TEST_MOCK_VERIFIED host=%s port=%u gen=%" PRIu32,
             net.moonraker_host, net.moonraker_port, net.endpoint_generation);
    if (mode == TEST_UNCERTAIN) {
      // Fixture must have delay_post>=1 + drop_post=true so both calls
      // enter the queue before the first deliberately uncertain response.
      bool first = send_mock_action(K_ACTION_GCODE,
                                    "M118 PANEL_TEST_UNCERTAIN_FIRST", &net);
      bool second =
          first && send_mock_action(K_ACTION_GCODE,
                                    "M118 PANEL_TEST_UNCERTAIN_SECOND", &net);
      ESP_LOGI(TAG,
               "PANEL_TEST_UNCERTAIN queued_first=%d queued_second=%d; inspect "
               "fixture request log and UI results",
               first, second);
      goto done;
    }
    klipper_client_fetch_files();
    klipper_client_fetch_macros();
    klipper_client_fetch_objects();
    if (!wait_lists(s, &net, 0) || !s->files_count || !s->macros_count ||
        !s->objects_count) {
      ESP_LOGE(TAG, "PANEL_TEST_ABORT fixture lists missing");
      goto done;
    }
    klipper_client_fetch_metadata(s->files[0]);
    int64_t began = esp_timer_get_time();
    do {
      klipper_client_snapshot(s);
      if (!s->metadata_loading)
        break;
      vTaskDelay(pdMS_TO_TICKS(100));
    } while (same_endpoint(&net) && esp_timer_get_time() - began < 10000000);
    klipper_client_metadata(m);
    print_metadata(m);
    if (!same_endpoint(&net) || !m->valid ||
        m->endpoint_generation != net.endpoint_generation) {
      ESP_LOGE(TAG, "PANEL_TEST_ABORT fixture metadata missing");
      goto done;
    }
    const char *scripts[] = {"G28", "M104 S170", "M140 S55", "PAUSE", "RESUME"};
    for (unsigned i = 0; i < sizeof(scripts) / sizeof(scripts[0]); ++i) {
      if (!send_mock_action(K_ACTION_GCODE, scripts[i], &net))
        goto done;
      vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (!send_mock_action(K_ACTION_MACRO, s->macros[0], &net))
      goto done;
    vTaskDelay(pdMS_TO_TICKS(500));
    if (!send_mock_action(K_ACTION_EXCLUDE, s->objects[0].name, &net))
      goto done;
    vTaskDelay(pdMS_TO_TICKS(500));
    if (!send_mock_action(K_ACTION_PRINT_FILE, s->files[0], &net))
      goto done;
    vTaskDelay(pdMS_TO_TICKS(500));
    if (!send_mock_action(K_ACTION_EMERGENCY, NULL, &net))
      goto done;
    vTaskDelay(pdMS_TO_TICKS(500));
    if (!send_mock_action(K_ACTION_SHUTDOWN, NULL, &net))
      goto done;
    ESP_LOGI(TAG, "PANEL_TEST_NORMAL enqueued all ten actions; inspect "
                  "simulator request log and UI results");
    goto done;
  }
  if (mode == READ_FILES)
    klipper_client_fetch_files();
  if (mode == READ_MACROS)
    klipper_client_fetch_macros();
  if (mode == READ_OBJECTS)
    klipper_client_fetch_objects();
  if (mode == READ_METADATA) {
    klipper_client_snapshot(s);
    if (!s->files_count) {
      printf("PANEL_METADATA first run panel files\n");
      goto done;
    }
    // The console normalizes input to lower case: use the retained filename,
    // never a filename parsed from that input.
    klipper_client_fetch_metadata(s->files[0]);
    int64_t began = esp_timer_get_time();
    do {
      klipper_client_snapshot(s);
      if (!s->metadata_loading)
        break;
      vTaskDelay(pdMS_TO_TICKS(100));
    } while (same_endpoint(&net) && esp_timer_get_time() - began < 10000000);
    if (same_endpoint(&net)) {
      klipper_client_metadata(m);
      print_metadata(m);
    }
    goto done;
  }
  if (!wait_lists(s, &net, mode)) {
    printf("PANEL_READ aborted or timed out\n");
    goto done;
  }
  if (mode == READ_FILES)
    for (unsigned i = 0; i < s->files_count; ++i)
      printf("PANEL_FILE %u %s\n", i, s->files[i]);
  if (mode == READ_MACROS)
    for (unsigned i = 0; i < s->macros_count; ++i)
      printf("PANEL_MACRO %u %s\n", i, s->macros[i]);
  if (mode == READ_OBJECTS)
    for (unsigned i = 0; i < s->objects_count; ++i)
      printf("PANEL_OBJECT %u %s excluded=%d active=%d bounds=%d\n", i,
             s->objects[i].name, s->objects[i].excluded, s->objects[i].active,
             s->objects[i].bounds_valid);
done:
  free(m);
  free(s);
  atomic_store(&busy, false);
  vTaskDelete(NULL);
}
bool panel_console_command(const char *command) {
  if (!command)
    return false;
  if (strcmp(command, "panel status") == 0) {
    print_status();
    return true;
  }
  if (strcmp(command, "panel") == 0 || strcmp(command, "panel help") == 0) {
    printf("panel status | panel files | panel macros | panel objects | panel "
           "metadata\n");
    return true;
  }
  unsigned mode = 0;
  if (strcmp(command, "panel files") == 0)
    mode = READ_FILES;
  else if (strcmp(command, "panel macros") == 0)
    mode = READ_MACROS;
  else if (strcmp(command, "panel objects") == 0)
    mode = READ_OBJECTS;
  else if (strcmp(command, "panel metadata") == 0)
    mode = READ_METADATA;
  else if (strcmp(command, "panel test") == 0 ||
           strcmp(command, "panel test normal") == 0)
    mode = TEST_NORMAL;
  else if (strcmp(command, "panel test uncertain") == 0)
    mode = TEST_UNCERTAIN;
  else
    return false;
  bool expected = false;
  if (!atomic_compare_exchange_strong(&busy, &expected, true)) {
    printf("PANEL_BUSY wait for previous panel command\n");
    return true;
  }
  if (xTaskCreate(worker, "panel_console", 8192, (void *)(uintptr_t)mode, 6,
                  NULL) != pdPASS) {
    atomic_store(&busy, false);
    printf("PANEL_ERROR could not start worker\n");
  }
  return true;
}
