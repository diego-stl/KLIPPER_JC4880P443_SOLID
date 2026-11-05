#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
// Deliberately accepts only an IPv4 address or DNS hostname, never URL/userinfo.
static inline bool panel_host_valid(const char *host)
{
    if (!host) return false;
    size_t length = strlen(host);
    if (!length || length > 63) return false;
    unsigned label = 0, dots = 0, value = 0, digits = 0;
    bool numeric = true;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)host[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '.')) return false;
        if (c == '.') {
            if (!label || host[i-1] == '-') return false;
            label = 0; ++dots;
        } else {
            if (!label && c == '-') return false;
            ++label;
        }
        if (!((c >= '0' && c <= '9') || c == '.')) numeric = false;
    }
    if (!label || host[length-1] == '-') return false;
    if (!numeric) return true;
    if (dots != 3) return false;
    for (size_t i = 0; i <= length; ++i) {
        unsigned char c = (unsigned char)host[i];
        if (c == '.' || c == 0) {
            if (!digits || digits > 3 || value > 255) return false;
            digits = value = 0;
        } else { value = value * 10 + c - '0'; ++digits; }
    }
    return true;
}
static inline bool panel_port_parse(const char *text, uint16_t *port)
{
    if (!text || !*text || !port) return false;
    unsigned value = 0;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9') return false;
        value = value * 10 + (unsigned)(*text - '0');
        if (value > 65535) return false;
    }
    if (!value) return false;
    *port = (uint16_t)value;
    return true;
}
// Byte limits match wifi_config_t, including the optional 64-hex PSK.
static inline bool panel_station_valid(const char *ssid, const char *password, bool requires_password)
{
    if (!ssid || !password) return false;
    size_t ssid_length = strlen(ssid), length = strlen(password);
    if (!ssid_length || ssid_length > 32) return false;
    if (!length) return !requires_password;
    if (length >= 8 && length <= 63) return true;
    if (length != 64) return false;
    for (size_t i = 0; i < length; ++i) {
        char c = password[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}
