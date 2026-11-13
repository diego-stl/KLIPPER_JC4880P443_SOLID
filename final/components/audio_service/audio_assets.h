#pragma once
#include <stddef.h>
#include <stdint.h>
#include "include/audio_clips.h"
typedef struct { const uint8_t *wav; size_t length; } audio_asset_t;
audio_asset_t audio_asset_get(audio_clip_t clip);
