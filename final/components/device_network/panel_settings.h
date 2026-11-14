#pragma once
#include "device_network.h"
#include "network_validation.h"

enum { PANEL_SETTINGS_V1_SIZE = 70, PANEL_SETTINGS_LEGACY_VERSION = 1, PANEL_SETTINGS_VERSION = 2 };

typedef struct {
    uint8_t version;
    uint8_t brightness;
    uint16_t sleep_minutes;
    uint16_t port;
    char host[64];
    uint8_t saver_brightness;
    uint8_t reserved;
} panel_settings_t;

// Version 2 appends fields to the exact stored version 1 prefix.
_Static_assert(offsetof(panel_settings_t,saver_brightness)==PANEL_SETTINGS_V1_SIZE &&
               sizeof(panel_settings_t)==72,"Unexpected panel settings layout");

static inline bool panel_settings_decode(panel_settings_t *settings,size_t size,uint8_t expected_version)
{
    bool legacy=expected_version==PANEL_SETTINGS_LEGACY_VERSION;
    if((!legacy && expected_version!=PANEL_SETTINGS_VERSION) ||
       size!=(legacy?PANEL_SETTINGS_V1_SIZE:sizeof(*settings)) || settings->version!=expected_version ||
       settings->brightness>100 || settings->sleep_minutes>120 || !settings->port ||
       !memchr(settings->host,0,sizeof(settings->host)) ||
       (settings->host[0] && !panel_host_valid(settings->host)))return false;
    if(legacy){
        settings->version=PANEL_SETTINGS_VERSION;
        settings->saver_brightness=DEVICE_NETWORK_DEFAULT_SAVER_BRIGHTNESS;
        settings->reserved=0;
    }
    return settings->saver_brightness<=100;
}
