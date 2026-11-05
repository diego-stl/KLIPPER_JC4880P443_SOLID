#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "audio_clips.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    bool ready, enabled, playing;
    uint8_t volume, queued;
    esp_err_t last_error;
    uint32_t tones_started, tones_completed, tones_aborted, requests_dropped, generation;
    bool settings_pending;
    uint32_t settings_revision, saved_revision;
    uint32_t clips_started, clips_completed, clips_aborted;
    audio_clip_t last_clip; // AUDIO_CLIP_COUNT until a voice actually starts.
} audio_service_status_t;
// Starts one asynchronous worker. No boot sound, no RX channel, no Klipper events.
esp_err_t audio_service_start(void);
void audio_service_get_status(audio_service_status_t *status);
// Setters publish immediately and defer NVS commit to the worker. ESP_OK means
// accepted; AUDIO_SETTINGS_SAVED confirms persistence. Failures remain in status.
esp_err_t audio_service_set_enabled(bool enabled);
esp_err_t audio_service_set_volume(uint8_t volume);
esp_err_t audio_service_test_tone(void);
esp_err_t audio_service_play(audio_clip_t clip);
const char *audio_service_clip_name(audio_clip_t clip);
// Immediately drives PA low, invalidates current work and flushes pending tones.
// Does not change the persisted enabled preference.
esp_err_t audio_service_stop(void);
// audio help/status/on/off/volume N/test/stop; explicit tests respect enabled.
bool audio_service_console_command(const char *command);
#ifdef __cplusplus
}
#endif
