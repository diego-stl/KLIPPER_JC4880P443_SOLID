#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    bool wifi_ready;
    bool has_ip;
    char ip[16];
    char error[128];
    char moonraker_host[64];
    uint16_t moonraker_port;
    uint8_t brightness;
    uint16_t sleep_minutes;
    uint32_t endpoint_generation;
} device_network_snapshot_t;
// Call after GUI is ready. No LVGL calls, no C6 firmware update.
esp_err_t device_network_start(void);
void device_network_snapshot(device_network_snapshot_t *snapshot);
// Brightness 0..100; sleep 0 (never) or 1..120 minutes. Persists only.
esp_err_t device_network_set_display(uint8_t brightness, uint16_t sleep_minutes);
bool device_network_console_active(void);
// Receives normalized commands not handled by the network console. Return true
// if handled. Invoked in the console worker with no snapshot lock held.
typedef bool (*device_network_console_handler_t)(const char *command);
void device_network_set_console_handler(device_network_console_handler_t handler);

#define DEVICE_NETWORK_SCAN_MAX 16
typedef enum {
    DEVICE_NETWORK_OP_NONE, DEVICE_NETWORK_OP_SCAN, DEVICE_NETWORK_OP_WIFI_SAVE,
    DEVICE_NETWORK_OP_ENDPOINT_SAVE, DEVICE_NETWORK_OP_ENDPOINT_TEST
} device_network_operation_t;
typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secured;
} device_network_ap_t;
typedef struct {
    bool ready;                  // Console worker can service requests, even without WiFi.
    bool busy;
    uint32_t revision;           // Changes at acceptance and completion.
    device_network_operation_t operation;
    esp_err_t result;
    char message[96];            // Fixed, redacted text; never remote response/credentials.
    uint16_t count;
    device_network_ap_t networks[DEVICE_NETWORK_SCAN_MAX];
} device_network_settings_snapshot_t;
// Nonblocking, bounded single-request mailbox. ESP_ERR_INVALID_STATE means not
// ready/busy; failed submission never replaces an accepted request. Callers may
// release/wipe all input buffers immediately after these calls return.
void device_network_settings_snapshot(device_network_settings_snapshot_t *out);
esp_err_t device_network_scan_async(void);
esp_err_t device_network_save_wifi_async(const char *ssid, const char *password, bool requires_password);
esp_err_t device_network_save_endpoint_async(const char *host, const char *port);
esp_err_t device_network_test_endpoint_async(const char *host, const char *port);
#ifdef __cplusplus
}
#endif
