#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "include/audio_clips.h"
typedef enum { AUDIO_NOT_OURS, AUDIO_HELP, AUDIO_STATUS, AUDIO_ON, AUDIO_OFF,
               AUDIO_VOLUME, AUDIO_TEST, AUDIO_STOP, AUDIO_SOUNDS, AUDIO_PLAY, AUDIO_INVALID } audio_command_t;
typedef struct { uint8_t version,enabled,volume,reserved; } audio_settings_t;
static inline bool audio_decode_settings(const audio_settings_t *settings,size_t size,
                                          bool *enabled,uint8_t *volume)
{
    if(!settings || !enabled || !volume || size!=sizeof(*settings) ||
       settings->version!=1 || settings->enabled>1 || settings->volume>100)return false;
    *enabled=settings->enabled;*volume=settings->volume;return true;
}
static inline audio_command_t audio_parse_command(const char *text,uint8_t *volume)
{
    if(!text || (strncmp(text,"audio",5)!=0) || (text[5] && text[5]!=' '))return AUDIO_NOT_OURS;
    if(!text[5] || !strcmp(text,"audio help"))return AUDIO_HELP;
    if(!strcmp(text,"audio status"))return AUDIO_STATUS;
    if(!strcmp(text,"audio on"))return AUDIO_ON;
    if(!strcmp(text,"audio off"))return AUDIO_OFF;
    if(!strcmp(text,"audio test"))return AUDIO_TEST;
    if(!strcmp(text,"audio stop"))return AUDIO_STOP;
    if(!strcmp(text,"audio sounds"))return AUDIO_SOUNDS;
    if(!strncmp(text,"audio play ",11)){
        audio_clip_t clip;return audio_clip_parse(text+11,&clip)?AUDIO_PLAY:AUDIO_INVALID;
    }
    if(strncmp(text,"audio volume ",13)!=0 || !volume)return AUDIO_INVALID;
    const char *p=text+13;unsigned value=0;
    if(!*p)return AUDIO_INVALID;
    for(;*p;++p){if(*p<'0'||*p>'9')return AUDIO_INVALID;value=value*10+(*p-'0');if(value>100)return AUDIO_INVALID;}
    *volume=(uint8_t)value;return AUDIO_VOLUME;
}
static inline bool audio_request_allowed(bool ready,bool enabled,uint8_t volume,
                                         uint32_t current,uint32_t request)
{
    return ready && enabled && volume>0 && current==request;
}
