#pragma once
#include <stdbool.h>
#include <string.h>
typedef enum {
    AUDIO_CLIP_WAKEUP, AUDIO_CLIP_PRINTING, AUDIO_CLIP_PAUSE, AUDIO_CLIP_CANCEL,
    AUDIO_CLIP_ERROR, AUDIO_CLIP_COMPLETE, AUDIO_CLIP_STANDBY,
    AUDIO_CLIP_TEMPERATURE, AUDIO_CLIP_TUNING, AUDIO_CLIP_MOVEMENT,
    AUDIO_CLIP_FILAMENT, AUDIO_CLIP_NETWORK, AUDIO_CLIP_SYSTEM, AUDIO_CLIP_COUNT
} audio_clip_t;
static inline const char *audio_clip_name_value(audio_clip_t clip)
{
    static const char *const names[AUDIO_CLIP_COUNT]={
        "wakeup","printing","pause","cancel","error","complete","standby",
        "temperature","tuning","movement","filament","network","system"
    };
    return (unsigned)clip<AUDIO_CLIP_COUNT?names[clip]:"none";
}
static inline bool audio_clip_parse(const char *name,audio_clip_t *clip)
{
    if(!name || !clip)return false;
    for(unsigned i=0;i<AUDIO_CLIP_COUNT;++i)if(!strcmp(name,audio_clip_name_value((audio_clip_t)i))){*clip=(audio_clip_t)i;return true;}
    return false;
}
