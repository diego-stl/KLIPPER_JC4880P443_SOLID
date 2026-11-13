#pragma once
#include "klipper_client.h"
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
  bool boot_handled, baseline_valid, source_live;
  klipper_state_t last_state;
  uint32_t endpoint_generation, reconnections, endpoint_changes, cancellations;
  uint32_t emitted, dropped, clip_counts[7];
} audio_events_status_t;
// Called by the GUI owner every ~200ms with actual backend snapshots. Does not
// mutate printer state or run I2C/PCM. The audio worker owns all playback.
void audio_events_update(const klipper_snapshot_t *snapshot);
void audio_events_get_status(audio_events_status_t *out);
// Read-only: "audio events" or "audio events status". No synthetic events.
bool audio_events_console_command(const char *command);
#ifdef __cplusplus
}
#endif
