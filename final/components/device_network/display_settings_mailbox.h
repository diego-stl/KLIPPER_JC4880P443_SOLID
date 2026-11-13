#pragma once
#include "device_network.h"

// Pure bounded state machine. Production callers hold s_snapshot_lock.
typedef struct {
    device_network_display_status_t status;
    uint8_t brightness;
    uint16_t sleep_minutes;
} display_settings_mailbox_t;
typedef struct { uint32_t revision; uint8_t brightness; uint16_t sleep_minutes; } display_settings_request_t;

static inline void display_settings_submit(display_settings_mailbox_t *box,uint8_t brightness,uint16_t sleep)
{
    if(box->brightness==brightness&&box->sleep_minutes==sleep&&box->status.result==ESP_OK)return;
    box->brightness=brightness;box->sleep_minutes=sleep;
    box->status.revision++;box->status.pending=true;box->status.result=ESP_OK;
}
static inline bool display_settings_begin(display_settings_mailbox_t *box,display_settings_request_t *request)
{
    if(!box->status.pending||box->status.busy)return false;
    *request=(display_settings_request_t){box->status.revision,box->brightness,box->sleep_minutes};
    box->status.busy=true;return true;
}
static inline void display_settings_complete(display_settings_mailbox_t *box,const display_settings_request_t *request,
                                             esp_err_t result,uint32_t elapsed_us)
{
    box->status.busy=false;box->status.writes++;
    box->status.completed_revision=request->revision;box->status.last_save_us=elapsed_us;
    if(elapsed_us>box->status.max_save_us)box->status.max_save_us=elapsed_us;
    if(result==ESP_OK)box->status.saved_revision=request->revision;
    if(request->revision==box->status.revision){box->status.pending=false;box->status.result=result;}
}
