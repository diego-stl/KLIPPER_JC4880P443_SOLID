#pragma once
#include <stdbool.h>
#include <stdint.h>

// Pure state policy. These values are mapped/checked against the public backend
// and audio enums by audio_events.c; no IDF or transport code is needed here.
typedef enum {
  AE_STARTUP,
  AE_OFFLINE,
  AE_STANDBY,
  AE_PRINTING,
  AE_PAUSED,
  AE_COMPLETE,
  AE_CANCELLED,
  AE_ERROR
} ae_state_t;
enum {
  AE_WAKEUP,
  AE_PRINTING_CLIP,
  AE_PAUSE_CLIP,
  AE_CANCEL_CLIP,
  AE_ERROR_CLIP,
  AE_COMPLETE_CLIP,
  AE_STANDBY_CLIP
};
typedef struct {
  bool audio_ready, audio_enabled;
  uint8_t volume;
  bool socket, initial_snapshot, available;
  uint32_t endpoint;
  ae_state_t state;
} ae_input_t;
typedef struct {
  bool seen_endpoint, boot_handled, baseline_valid, source_live, ever_baseline;
  ae_state_t last_state;
  uint32_t endpoint, reconnections, endpoint_changes, cancellations;
} ae_model_t;
typedef struct {
  bool cancel;
  int boot_clip, state_clip;
  unsigned suppressed;
} ae_effect_t;

static inline int ae_transition(ae_state_t previous, ae_state_t next) {
  if (previous == next)
    return -1;
  if (next == AE_PRINTING)
    return AE_PRINTING_CLIP;
  if (previous == AE_PRINTING && next == AE_PAUSED)
    return AE_PAUSE_CLIP;
  if (next == AE_CANCELLED)
    return AE_CANCEL_CLIP;
  if (next == AE_ERROR)
    return AE_ERROR_CLIP;
  if (next == AE_COMPLETE)
    return AE_COMPLETE_CLIP;
  if (next == AE_STANDBY)
    return AE_STANDBY_CLIP;
  return -1;
}
static inline ae_effect_t ae_update(ae_model_t *model, const ae_input_t *in) {
  ae_effect_t effect = {.boot_clip = -1, .state_clip = -1};
  const bool audible = in->audio_ready && in->audio_enabled && in->volume > 0;
  if (!model->seen_endpoint) {
    model->seen_endpoint = true;
    model->endpoint = in->endpoint;
  } else if (model->endpoint != in->endpoint) {
    // Wakeup belongs to this device, not to a printer. Initial setup before
    // any authoritative printer baseline must not cut that startup clip.
    effect.cancel = model->source_live || model->baseline_valid;
    model->endpoint = in->endpoint;
    ++model->endpoint_changes;
    model->baseline_valid = false;
    model->source_live = false;
  }
  if (!model->boot_handled && in->audio_ready) {
    model->boot_handled = true;
    if (audible)
      effect.boot_clip = AE_WAKEUP;
    else
      ++effect.suppressed;
  }
  const bool job_state = in->state >= AE_STANDBY && in->state <= AE_ERROR;
  const bool live =
      in->socket && job_state &&
      ((in->initial_snapshot && in->available) || in->state == AE_ERROR);
  if (!live) {
    if (model->source_live)
      effect.cancel = true;
    model->source_live = false;
    model->baseline_valid = false;
  } else {
    if (!model->baseline_valid) {
      if (model->ever_baseline)
        ++model->reconnections;
      model->ever_baseline = true;
      model->baseline_valid = true;
    } else {
      const int clip = ae_transition(model->last_state, in->state);
      if (clip >= 0) {
        if (audible)
          effect.state_clip = clip;
        else
          ++effect.suppressed;
      }
    }
    // Baselines move while muted or not ready, so no backlog is synthesized
    // on unmute or when the codec becomes ready later.
    model->last_state = in->state;
    model->source_live = true;
  }
  if (effect.cancel)
    ++model->cancellations;
  return effect;
}
