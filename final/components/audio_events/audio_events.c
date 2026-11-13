#include "audio_events.h"
#include "audio_events_logic.h"
#include "audio_service.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

_Static_assert((int)KLIPPER_STARTUP == (int)AE_STARTUP &&
                   (int)KLIPPER_OFFLINE == (int)AE_OFFLINE &&
                   (int)KLIPPER_STANDBY == (int)AE_STANDBY &&
                   (int)KLIPPER_PRINTING == (int)AE_PRINTING &&
                   (int)KLIPPER_PAUSED == (int)AE_PAUSED &&
                   (int)KLIPPER_COMPLETE == (int)AE_COMPLETE &&
                   (int)KLIPPER_CANCELLED == (int)AE_CANCELLED &&
                   (int)KLIPPER_ERROR == (int)AE_ERROR,
               "Backend state mapping changed");
_Static_assert((int)AUDIO_CLIP_WAKEUP == (int)AE_WAKEUP &&
                   (int)AUDIO_CLIP_PRINTING == (int)AE_PRINTING_CLIP &&
                   (int)AUDIO_CLIP_PAUSE == (int)AE_PAUSE_CLIP &&
                   (int)AUDIO_CLIP_CANCEL == (int)AE_CANCEL_CLIP &&
                   (int)AUDIO_CLIP_ERROR == (int)AE_ERROR_CLIP &&
                   (int)AUDIO_CLIP_COMPLETE == (int)AE_COMPLETE_CLIP &&
                   (int)AUDIO_CLIP_STANDBY == (int)AE_STANDBY_CLIP,
               "Audio clip mapping changed");

static const char *TAG = "audio_events";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static ae_model_t s_model;
static audio_events_status_t s_status;

// GUI update is the single policy owner. Console only copies the short
// snapshot.
static void submit(int clip, uint32_t endpoint) {
  if (clip < 0 || clip > AE_STANDBY_CLIP)
    return;
  esp_err_t result = clip == AE_WAKEUP ? audio_service_play(AUDIO_CLIP_WAKEUP)
                                     : audio_service_play_printer((audio_clip_t)clip);
  portENTER_CRITICAL(&s_lock);
  if (result == ESP_OK) {
    ++s_status.emitted;
    ++s_status.clip_counts[clip];
  } else
    ++s_status.dropped;
  portEXIT_CRITICAL(&s_lock);
  ESP_LOGI(TAG, "AUDIO_EVENT clip=%s endpoint=%" PRIu32 " accepted=%d",
           audio_clip_name_value((audio_clip_t)clip), endpoint, result == ESP_OK);
}
void audio_events_update(const klipper_snapshot_t *snapshot) {
  if (!snapshot)
    return;
  audio_service_status_t audio = {0};
  audio_service_get_status(&audio);
  ae_input_t input = {.audio_ready = audio.ready,
                      .audio_enabled = audio.enabled,
                      .volume = audio.volume,
                      .socket = snapshot->socket_connected,
                      .initial_snapshot = snapshot->initial_snapshot,
                      .available = snapshot->available,
                      .endpoint = snapshot->endpoint_generation,
                      .state = (ae_state_t)snapshot->state};
  ae_effect_t effect = ae_update(&s_model, &input);
  if (effect.cancel) {
    audio_service_stop_printer();
    ESP_LOGI(TAG,
             "AUDIO_EVENTS_INVALIDATED endpoint=%" PRIu32 " source_live=%d",
             input.endpoint, s_model.source_live);
  }
  portENTER_CRITICAL(&s_lock);
  s_status.boot_handled = s_model.boot_handled;
  s_status.baseline_valid = s_model.baseline_valid;
  s_status.source_live = s_model.source_live;
  s_status.last_state = (klipper_state_t)s_model.last_state;
  s_status.endpoint_generation = s_model.endpoint;
  s_status.reconnections = s_model.reconnections;
  s_status.endpoint_changes = s_model.endpoint_changes;
  s_status.cancellations = s_model.cancellations;
  s_status.dropped += effect.suppressed;
  portEXIT_CRITICAL(&s_lock);
  submit(effect.boot_clip, input.endpoint);
  submit(effect.state_clip, input.endpoint);
}
void audio_events_get_status(audio_events_status_t *out) {
  if (!out)
    return;
  portENTER_CRITICAL(&s_lock);
  *out = s_status;
  portEXIT_CRITICAL(&s_lock);
}
bool audio_events_console_command(const char *command) {
  if (!command || (strcmp(command, "audio events") &&
                   strcmp(command, "audio events status")))
    return false;
  audio_events_status_t s;
  audio_events_get_status(&s);
  printf("AUDIO_EVENTS boot=%d baseline=%d live=%d state=%d gen=%" PRIu32
         " reconnect=%" PRIu32 " endpoint_changes=%" PRIu32
         " cancellations=%" PRIu32 " emitted=%" PRIu32 " dropped=%" PRIu32 "\n",
         s.boot_handled, s.baseline_valid, s.source_live, s.last_state,
         s.endpoint_generation, s.reconnections, s.endpoint_changes,
         s.cancellations, s.emitted, s.dropped);
  for (unsigned i = 0; i <= AE_STANDBY_CLIP; ++i)
    printf("AUDIO_EVENTS_CLIP %s=%" PRIu32 "\n",
           audio_clip_name_value((audio_clip_t)i), s.clip_counts[i]);
  return true;
}
