#include "../audio_events_logic.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned assertions;
#define CHECK(expression)                                                      \
  do {                                                                         \
    ++assertions;                                                              \
    if (!(expression)) {                                                       \
      fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expression);            \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
static ae_input_t online(ae_state_t state) {
  ae_input_t in = {.audio_ready = true,
                   .audio_enabled = true,
                   .volume = 50,
                   .socket = true,
                   .initial_snapshot = true,
                   .available = true,
                   .endpoint = 1,
                   .state = state};
  return in;
}
static void boot_and_initial_baseline(void) {
  ae_model_t model = {0};
  ae_input_t in = {.endpoint = 0, .state = AE_OFFLINE};
  ae_effect_t out = ae_update(&model, &in);
  CHECK(out.boot_clip == -1);
  CHECK(!out.cancel);
  CHECK(!model.baseline_valid);
  in.audio_ready = true;
  in.audio_enabled = true;
  in.volume = 50;
  out = ae_update(&model, &in);
  CHECK(out.boot_clip == AE_WAKEUP);
  CHECK(!out.cancel);
  CHECK(model.boot_handled);
  out = ae_update(&model, &in);
  CHECK(out.boot_clip == -1);
  CHECK(!out.cancel);
  // Initial setup (0 -> configured host) must not cut the global startup clip.
  in.endpoint = 1;
  in.socket = true;
  out = ae_update(&model, &in);
  CHECK(!out.cancel);
  CHECK(out.state_clip == -1);
  in = online(AE_PRINTING);
  out = ae_update(&model, &in);
  CHECK(!out.cancel);
  CHECK(out.state_clip == -1);
  CHECK(model.baseline_valid);
  CHECK(model.last_state == AE_PRINTING);
  CHECK(model.reconnections == 0);
  out = ae_update(&model, &in);
  CHECK(out.state_clip == -1);
}
static void muted_boot_never_replays(void) {
  for (unsigned reason = 0; reason < 2; ++reason) {
    ae_model_t model = {0};
    ae_input_t in = online(AE_STANDBY);
    if (reason == 0)
      in.audio_enabled = false;
    else
      in.volume = 0;
    ae_effect_t out = ae_update(&model, &in);
    CHECK(out.boot_clip == -1);
    CHECK(out.suppressed == 1);
    CHECK(model.boot_handled);
    in.audio_enabled = true;
    in.volume = 66;
    out = ae_update(&model, &in);
    CHECK(out.boot_clip == -1);
    CHECK(out.state_clip == -1);
  }
}
static void transitions_and_muting(void) {
  ae_model_t model = {0};
  ae_input_t in = online(AE_STANDBY);
  ae_update(&model, &in);
  in.state = AE_PRINTING;
  ae_effect_t out = ae_update(&model, &in);
  CHECK(out.state_clip == AE_PRINTING_CLIP);
  in.state = AE_PAUSED;
  out = ae_update(&model, &in);
  CHECK(out.state_clip == AE_PAUSE_CLIP);
  in.state = AE_PRINTING;
  out = ae_update(&model, &in);
  CHECK(out.state_clip == AE_PRINTING_CLIP);
  in.audio_enabled = false;
  in.state = AE_COMPLETE;
  out = ae_update(&model, &in);
  CHECK(out.state_clip == -1);
  CHECK(out.suppressed == 1);
  CHECK(model.last_state == AE_COMPLETE);
  in.audio_enabled = true;
  out = ae_update(&model, &in);
  CHECK(out.state_clip == -1);
  in.volume = 0;
  in.state = AE_STANDBY;
  out = ae_update(&model, &in);
  CHECK(out.state_clip == -1);
  CHECK(model.last_state == AE_STANDBY);
  in.volume = 66;
  out = ae_update(&model, &in);
  CHECK(out.state_clip == -1);
  in.state = AE_CANCELLED;
  out = ae_update(&model, &in);
  CHECK(out.state_clip == AE_CANCEL_CLIP);
  in.state = AE_COMPLETE;
  out = ae_update(&model, &in);
  CHECK(out.state_clip == AE_COMPLETE_CLIP);
  in.state = AE_STANDBY;
  out = ae_update(&model, &in);
  CHECK(out.state_clip == AE_STANDBY_CLIP);
  in.state = AE_PAUSED;
  out = ae_update(&model, &in);
  CHECK(out.state_clip == -1); // Only PRINTING -> PAUSED speaks.
}
static void reconnect_endpoint_and_errors(void) {
  ae_model_t model = {0};
  ae_input_t in = online(AE_PRINTING);
  ae_update(&model, &in);
  in.socket = false;
  in.available = false;
  in.initial_snapshot = false;
  ae_effect_t out = ae_update(&model, &in);
  CHECK(out.cancel);
  CHECK(!model.baseline_valid);
  CHECK(out.state_clip == -1);
  CHECK(model.cancellations == 1);
  out = ae_update(&model, &in);
  CHECK(!out.cancel);
  CHECK(model.cancellations == 1);
  in = online(AE_PRINTING);
  out = ae_update(&model, &in);
  CHECK(out.state_clip == -1);
  CHECK(model.reconnections == 1);
  in.state = AE_PAUSED;
  out = ae_update(&model, &in);
  CHECK(out.state_clip == AE_PAUSE_CLIP);
  in.endpoint = 2;
  in.state = AE_PRINTING;
  out = ae_update(&model, &in);
  CHECK(out.cancel);
  CHECK(out.state_clip == -1);
  CHECK(model.endpoint_changes == 1);
  CHECK(model.last_state == AE_PRINTING);
  // Backend shutdown is authoritative while the socket remains open even
  // though ready/available and subscription flags are cleared.
  in.state = AE_ERROR;
  in.available = false;
  in.initial_snapshot = false;
  out = ae_update(&model, &in);
  CHECK(out.state_clip == AE_ERROR_CLIP);
  CHECK(!out.cancel);
  CHECK(model.baseline_valid);
  out = ae_update(&model, &in);
  CHECK(out.state_clip == -1); // Error text changes cannot speak twice.
  in.socket = false;
  out = ae_update(&model, &in);
  CHECK(out.cancel);
  CHECK(out.state_clip == -1);
  in.socket = true;
  out = ae_update(&model, &in);
  CHECK(out.state_clip == -1);
  CHECK(model.baseline_valid); // Error at reconnect is baseline.
  in.state = AE_STARTUP;
  out = ae_update(&model, &in);
  CHECK(out.cancel);
  CHECK(out.state_clip == -1);
  in = online(AE_PRINTING);
  in.endpoint = 2;
  out = ae_update(&model, &in);
  CHECK(out.state_clip == -1);
}
static void boot_error_and_delayed_codec(void) {
  ae_model_t model = {0};
  ae_input_t in = online(AE_ERROR);
  in.available = false;
  in.initial_snapshot = false;
  ae_effect_t out = ae_update(&model, &in);
  CHECK(out.boot_clip == AE_WAKEUP);
  CHECK(out.state_clip == -1);
  CHECK(model.baseline_valid);
  ae_model_t delayed = {0};
  in = online(AE_STANDBY);
  in.audio_ready = false;
  out = ae_update(&delayed, &in);
  CHECK(!delayed.boot_handled);
  CHECK(out.state_clip == -1);
  in.state = AE_PRINTING;
  out = ae_update(&delayed, &in);
  CHECK(out.state_clip == -1);
  CHECK(out.suppressed == 1);
  CHECK(delayed.last_state == AE_PRINTING);
  in.audio_ready = true;
  out = ae_update(&delayed, &in);
  CHECK(out.boot_clip == AE_WAKEUP);
  CHECK(out.state_clip == -1);
}
static void rapid_authoritative_changes(void) {
  ae_model_t model = {0};
  ae_input_t in = online(AE_STANDBY);
  ae_update(&model, &in);
  const ae_state_t states[] = {AE_PRINTING, AE_PAUSED,  AE_PRINTING,
                               AE_COMPLETE, AE_STANDBY, AE_CANCELLED,
                               AE_ERROR};
  const int clips[] = {AE_PRINTING_CLIP, AE_PAUSE_CLIP,   AE_PRINTING_CLIP,
                       AE_COMPLETE_CLIP, AE_STANDBY_CLIP, AE_CANCEL_CLIP,
                       AE_ERROR_CLIP};
  for (unsigned i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
    in.state = states[i];
    ae_effect_t out = ae_update(&model, &in);
    CHECK(out.state_clip == clips[i]);
    CHECK(!out.cancel);
    out = ae_update(&model, &in);
    CHECK(out.state_clip == -1);
  }
}
int main(void) {
  boot_and_initial_baseline();
  muted_boot_never_replays();
  transitions_and_muting();
  reconnect_endpoint_and_errors();
  boot_error_and_delayed_codec();
  rapid_authoritative_changes();
  printf("PASS audio_events: %u assertions, "
         "boot/mute/baselines/reconnect/endpoint/error/rapid transitions\n",
         assertions);
  return 0;
}
