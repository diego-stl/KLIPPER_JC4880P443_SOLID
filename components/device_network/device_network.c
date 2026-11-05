#include "sdkconfig.h"
#include "device_network.h"
#include "network_validation.h"
#include "serial_menu.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_event.h"
#include "esp_hosted.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "ping/ping_sock.h"

#define GOT_IP BIT0
#define DISCONNECTED BIT1
#define ASSOCIATED BIT2

// WiFi configuration changes only this namespace/key; panel settings are separate.
#define WIFI_NAMESPACE "jc_wifi"
#define WIFI_KEY "station_v1"

typedef struct {
    uint8_t version;
    uint8_t ssid_len;
    uint8_t password_len;
    uint8_t reserved;
    uint8_t ssid[32];
    uint8_t password[64];
} saved_station_t;

static const char *TAG = "device_network";
static portMUX_TYPE s_snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
static device_network_snapshot_t s_snapshot = {.moonraker_port=7125, .brightness=70, .sleep_minutes=1};
static bool s_started;
static bool s_console_active;
static device_network_console_handler_t s_console_handler;
static int64_t s_next_reconnect;
static void reconnect_if_needed(void);
static void configure_moonraker(void);
static esp_err_t store_endpoint(const char *host,uint16_t port);
static void settings_process_request(void);
static esp_err_t save_station_and_connect(const saved_station_t *station);
typedef struct {
    device_network_operation_t operation;
    saved_station_t station;
    char host[64];
    uint16_t port;
} settings_request_t;
static QueueHandle_t s_settings_queue;
static device_network_settings_snapshot_t s_settings_status;

void device_network_settings_snapshot(device_network_settings_snapshot_t *out)
{
    if (!out) return;
    portENTER_CRITICAL(&s_snapshot_lock);
    *out=s_settings_status;
    portEXIT_CRITICAL(&s_snapshot_lock);
}
static esp_err_t settings_submit(const settings_request_t *request)
{
    portENTER_CRITICAL(&s_snapshot_lock);
    bool accepted=s_settings_queue && s_settings_status.ready && !s_settings_status.busy &&
        (s_snapshot.wifi_ready || request->operation==DEVICE_NETWORK_OP_ENDPOINT_SAVE);
    if (accepted) {
        s_settings_status.busy=true;
        s_settings_status.operation=request->operation;
        s_settings_status.result=ESP_OK;
        snprintf(s_settings_status.message,sizeof(s_settings_status.message),"Solicitud pendiente");
        ++s_settings_status.revision;
    }
    portEXIT_CRITICAL(&s_snapshot_lock);
    if (!accepted) return ESP_ERR_INVALID_STATE;
    if (xQueueSend(s_settings_queue,request,0)==pdTRUE) return ESP_OK;
    portENTER_CRITICAL(&s_snapshot_lock);
    s_settings_status.busy=false;
    s_settings_status.result=ESP_ERR_NO_MEM;
    ++s_settings_status.revision;
    portEXIT_CRITICAL(&s_snapshot_lock);
    return ESP_ERR_NO_MEM;
}
esp_err_t device_network_scan_async(void)
{
    settings_request_t request={.operation=DEVICE_NETWORK_OP_SCAN};
    return settings_submit(&request);
}
esp_err_t device_network_save_wifi_async(const char *ssid,const char *password,bool requires_password)
{
    if (!panel_station_valid(ssid,password,requires_password)) return ESP_ERR_INVALID_ARG;
    settings_request_t request={.operation=DEVICE_NETWORK_OP_WIFI_SAVE,.station={.version=1}};
    request.station.ssid_len=strlen(ssid);request.station.password_len=strlen(password);
    memcpy(request.station.ssid,ssid,request.station.ssid_len);
    memcpy(request.station.password,password,request.station.password_len);
    esp_err_t result=settings_submit(&request);
    volatile uint8_t *p=(volatile uint8_t *)&request;
    for (size_t i=0;i<sizeof(request);++i) p[i]=0;
    return result;
}
static esp_err_t endpoint_submit(device_network_operation_t operation,const char *host,const char *port_text)
{
    uint16_t port;
    if (!panel_host_valid(host) || !panel_port_parse(port_text,&port)) return ESP_ERR_INVALID_ARG;
    settings_request_t request={.operation=operation,.port=port};
    snprintf(request.host,sizeof(request.host),"%s",host);
    return settings_submit(&request);
}
esp_err_t device_network_save_endpoint_async(const char *host,const char *port)
{ return endpoint_submit(DEVICE_NETWORK_OP_ENDPOINT_SAVE,host,port); }
esp_err_t device_network_test_endpoint_async(const char *host,const char *port)
{ return endpoint_submit(DEVICE_NETWORK_OP_ENDPOINT_TEST,host,port); }
static void set_error(const char *message)
{
    portENTER_CRITICAL(&s_snapshot_lock);
    snprintf(s_snapshot.error, sizeof(s_snapshot.error), "%s", message);
    portEXIT_CRITICAL(&s_snapshot_lock);
}
void device_network_snapshot(device_network_snapshot_t *out)
{
    if (!out) return;
    portENTER_CRITICAL(&s_snapshot_lock); *out=s_snapshot; portEXIT_CRITICAL(&s_snapshot_lock);
}
bool device_network_console_active(void)
{
    portENTER_CRITICAL(&s_snapshot_lock); bool active=s_console_active; portEXIT_CRITICAL(&s_snapshot_lock);
    return active;
}
static void console_active(bool active)
{
    portENTER_CRITICAL(&s_snapshot_lock); s_console_active=active; portEXIT_CRITICAL(&s_snapshot_lock);
}
void device_network_set_console_handler(device_network_console_handler_t handler)
{
    portENTER_CRITICAL(&s_snapshot_lock);
    s_console_handler=handler;
    portEXIT_CRITICAL(&s_snapshot_lock);
}
static bool extension_command(const char *command)
{
    portENTER_CRITICAL(&s_snapshot_lock);
    device_network_console_handler_t handler=s_console_handler;
    portEXIT_CRITICAL(&s_snapshot_lock);
    return handler && handler(command);
}
static EventGroupHandle_t s_events;
static esp_netif_t *s_netif;
static bool s_saved;
static bool s_want_connection;
static volatile uint16_t s_last_reason;
static SemaphoreHandle_t s_ping_done;
static uint32_t s_ping_received;

static void wipe(void *memory, size_t size)
{
    volatile uint8_t *p = memory;
    while (size--) *p++ = 0;
}

static void network_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    // No credentials or LVGL calls in this callback. Worker owns connection APIs.
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        xEventGroupSetBits(s_events, ASSOCIATED);
        ESP_LOGI(TAG, "STA_ASSOCIATED (SSID/BSSID omitidos)");
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_last_reason = ((wifi_event_sta_disconnected_t *)data)->reason;
        xEventGroupClearBits(s_events, GOT_IP | ASSOCIATED);
        portENTER_CRITICAL(&s_snapshot_lock);
        s_snapshot.has_ip=false; s_snapshot.ip[0]=0;
        snprintf(s_snapshot.error,sizeof(s_snapshot.error),"WiFi desconectado (reason %u)",s_last_reason);
        portEXIT_CRITICAL(&s_snapshot_lock);
        xEventGroupSetBits(s_events, DISCONNECTED);
        ESP_LOGW(TAG, "STA_DISCONNECTED reason=%u", s_last_reason);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        xEventGroupClearBits(s_events, GOT_IP);
        portENTER_CRITICAL(&s_snapshot_lock);
        s_snapshot.has_ip=false; s_snapshot.ip[0]=0;
        snprintf(s_snapshot.error,sizeof(s_snapshot.error),"WiFi sin direccion IP");
        portEXIT_CRITICAL(&s_snapshot_lock);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        ESP_LOGI(TAG, "GOT_IP=" IPSTR " mask=" IPSTR " gateway=" IPSTR,
                 IP2STR(&event->ip_info.ip), IP2STR(&event->ip_info.netmask),
                 IP2STR(&event->ip_info.gw));
        portENTER_CRITICAL(&s_snapshot_lock);
        s_snapshot.has_ip=true;
        snprintf(s_snapshot.ip,sizeof(s_snapshot.ip),IPSTR,IP2STR(&event->ip_info.ip));
        s_snapshot.error[0]=0;
        portEXIT_CRITICAL(&s_snapshot_lock);
        xEventGroupSetBits(s_events, GOT_IP);
    }
}

static esp_err_t wifi_connection_init(void)
{
    s_events = xEventGroupCreate();
    s_ping_done = xSemaphoreCreateBinary();
    if (!s_events || !s_ping_done) return ESP_ERR_NO_MEM;
    s_netif = esp_netif_create_default_wifi_sta();
    if (!s_netif) return ESP_ERR_NO_MEM;
    esp_err_t error = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, network_event, NULL);
    if (error == ESP_OK) error = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, network_event, NULL);
    return error;
}

static esp_err_t load_station(saved_station_t *station)
{
    nvs_handle_t handle;
    esp_err_t error = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &handle);
    if (error != ESP_OK) return error;
    size_t size = sizeof(*station);
    error = nvs_get_blob(handle, WIFI_KEY, station, &size);
    nvs_close(handle);
    if (error == ESP_OK && (size != sizeof(*station) || station->version != 1 ||
        station->ssid_len == 0 || station->ssid_len > 32 || station->password_len > 64)) {
        error = ESP_ERR_INVALID_RESPONSE;
    }
    return error;
}

static esp_err_t store_station(const saved_station_t *station)
{
    nvs_handle_t handle;
    esp_err_t error = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;
    error = nvs_set_blob(handle, WIFI_KEY, station, sizeof(*station));
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error;
}

static esp_err_t apply_station(const saved_station_t *station)
{
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, station->ssid, station->ssid_len);
    memcpy(config.sta.password, station->password, station->password_len);
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;
    // The C6 keeps its active config in RAM; persistence is explicitly on P4 NVS.
    esp_err_t error = esp_wifi_set_config(WIFI_IF_STA, &config);
    wipe(&config, sizeof(config));
    return error;
}

static void ping_end(esp_ping_handle_t session, void *arg)
{
    esp_ping_get_profile(session, ESP_PING_PROF_REPLY, &s_ping_received, sizeof(s_ping_received));
    xSemaphoreGive(s_ping_done);
}

static bool ping_gateway(uint32_t address)
{
    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    config.count = 3;
    config.interval_ms = 500;
    config.timeout_ms = 1000;
    ip_addr_set_ip4_u32(&config.target_addr, address);
    esp_ping_callbacks_t callbacks = {.on_ping_end = ping_end};
    esp_ping_handle_t session = NULL;
    xSemaphoreTake(s_ping_done, 0);
    s_ping_received = 0;
    esp_err_t error = esp_ping_new_session(&config, &callbacks, &session);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "PING_GATEWAY init: %s", esp_err_to_name(error));
        return false;
    }
    error = esp_ping_start(session);
    bool completed = error == ESP_OK && xSemaphoreTake(s_ping_done, pdMS_TO_TICKS(7000)) == pdTRUE;
    if (!completed) {
        esp_ping_stop(session);
        xSemaphoreTake(s_ping_done, pdMS_TO_TICKS(2000));
    }
    esp_ping_delete_session(session);
    ESP_LOGI(TAG, "PING_GATEWAY replies=%lu/3 completed=%d", (unsigned long)s_ping_received, completed);
    return completed && s_ping_received > 0;
}

static bool check_external_tcp(void)
{
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
    struct addrinfo *addresses = NULL;
    int result = getaddrinfo("example.com", "443", &hints, &addresses);
    if (result != 0 || !addresses) {
        ESP_LOGW(TAG, "DNS example.com: fallo (%d)", result);
        if (addresses) freeaddrinfo(addresses);
        return false;
    }
    ESP_LOGI(TAG, "DNS example.com: OK");
    int socket_fd = socket(addresses->ai_family, addresses->ai_socktype, addresses->ai_protocol);
    bool connected = false;
    if (socket_fd >= 0) {
        int flags = fcntl(socket_fd, F_GETFL, 0);
        if (flags >= 0 && fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == 0) {
            result = connect(socket_fd, addresses->ai_addr, addresses->ai_addrlen);
            if (result == 0) connected = true;
            else if (errno == EINPROGRESS) {
                fd_set ready;
                FD_ZERO(&ready);
                FD_SET(socket_fd, &ready);
                struct timeval timeout = {.tv_sec = 5};
                if (select(socket_fd + 1, NULL, &ready, NULL, &timeout) > 0) {
                    int socket_error = 0;
                    socklen_t size = sizeof(socket_error);
                    connected = getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &socket_error, &size) == 0 && socket_error == 0;
                }
            }
        }
        close(socket_fd);
    }
    freeaddrinfo(addresses);
    ESP_LOGI(TAG, "TCP example.com:443: %s (no prueba TLS/HTTP)", connected ? "OK" : "FALLO");
    return connected;
}

static void check_connectivity(void)
{
    esp_netif_ip_info_t info = {0};
    if (!(xEventGroupGetBits(s_events) & GOT_IP)) {
        ESP_LOGW(TAG, "Sin IP; usar config o connect antes de check");
        return;
    }
    esp_err_t error = esp_netif_get_ip_info(s_netif, &info);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Consultar IP: %s", esp_err_to_name(error));
        return;
    }
    bool gateway = info.gw.addr != 0 && ping_gateway(info.gw.addr);
    bool external = check_external_tcp();
    ESP_LOGI(TAG, "CONNECTIVITY_RESULT gateway=%s dns_tcp_external=%s",
             gateway ? "OK" : "FALLO", external ? "OK" : "FALLO");
}

static bool connect_station(void)
{
    if (xEventGroupGetBits(s_events) & GOT_IP) return true;
    for (unsigned attempt = 1; attempt <= 3; ++attempt) {
        xEventGroupClearBits(s_events, GOT_IP | DISCONNECTED | ASSOCIATED);
        ESP_LOGI(TAG, "CONNECT_ATTEMPT %u/3", attempt);
        esp_err_t error = esp_wifi_connect();
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "wifi connect RPC: %s", esp_err_to_name(error));
        } else {
            EventBits_t bits = xEventGroupWaitBits(s_events, GOT_IP | DISCONNECTED,
                                                  pdFALSE, pdFALSE, pdMS_TO_TICKS(20000));
            if (bits & GOT_IP) return true;
            ESP_LOGW(TAG, "Sin IP; reason=%u; esperando antes de reintentar", s_last_reason);
            esp_wifi_disconnect();
        }
        vTaskDelay(pdMS_TO_TICKS(1000U << (attempt - 1)));
    }
    set_error("WiFi sin IP; reintentando automaticamente");
    ESP_LOGE(TAG, "CONNECT_FAILED; GUI continua; se conserva reconexion automatica");
    return false;
}

// Reads USB without echoing the actual contents of credential fields.
// Commands are echoed; password and SSID are represented only by '*'. ESC cancels.
enum { LINE_CANCELLED = -1, LINE_IDLE = -2, LINE_TOO_LONG = -3 };
static int read_line(char *line, size_t capacity, bool masked, bool wait)
{
    size_t length = 0;
    bool overflow = false;
    static bool skip_lf;
    for (;;) {
        uint8_t byte;
        int received = usb_serial_jtag_read_bytes(&byte, 1, pdMS_TO_TICKS(100));
        if (received <= 0) {
            // Only this worker calls WiFi/scan/HTTP. Even while a console prompt
            // waits for input, GUI requests remain asynchronous and serialized.
            settings_process_request();
            reconnect_if_needed();
            if (!wait && length == 0) return LINE_IDLE;
            continue;
        }
        if (skip_lf && byte == '\n') { skip_lf = false; continue; }
        skip_lf = false;
        if (byte == '\r' || byte == '\n') {
            skip_lf = byte == '\r';
            line[length] = 0;
            printf("\n");
            fflush(stdout);
            return overflow ? LINE_TOO_LONG : (int)length;
        }
        if (byte == 27) {
            wipe(line, capacity);
            printf("\nCancelado.\n");
            fflush(stdout);
            return LINE_CANCELLED;
        }
        if (byte == 8 || byte == 127) {
            if (length) { --length; printf("\b \b"); fflush(stdout); }
            continue;
        }
        if (!masked && byte == '\t') byte = ' ';
        if (byte < 32) continue;
        if (length + 1 < capacity) {
            line[length++] = (char)byte;
            putchar(masked ? '*' : byte);
            fflush(stdout);
        } else overflow = true;
    }
}

static const char *network_security(wifi_auth_mode_t mode)
{
    switch (mode) {
    case WIFI_AUTH_OPEN: return "abierta";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    default: return "otra seguridad";
    }
}

static bool select_network(saved_station_t *station, bool *requires_password)
{
    // Heap allocation keeps the scan records off the worker's bounded stack.
    const uint16_t limit = 32;
    wifi_ap_record_t *networks = calloc(limit, sizeof(*networks));
    if (!networks) {
        ESP_LOGE(TAG, "Sin memoria para la lista de redes; NVS sin cambios");
        return false;
    }
    bool selected = false;
    for (;;) {
        printf("\nEscaneando redes Wi-Fi de 2,4 GHz...\n"); fflush(stdout);
        wifi_scan_config_t config = {.show_hidden = false};
        uint16_t count = limit;
        esp_err_t error = esp_wifi_scan_start(&config, true);
        if (error == ESP_OK) error = esp_wifi_scan_get_ap_records(&count, networks);
        esp_wifi_clear_ap_list();
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "Escaneo: %s; NVS sin cambios", esp_err_to_name(error));
            break;
        }
        // IDF returns the strongest APs first. Group repeated SSID/security pairs,
        // retaining the strongest; do not bind the saved station to one BSSID.
        uint16_t visible = 0;
        for (uint16_t i = 0; i < count; ++i) {
            size_t length = strnlen((const char *)networks[i].ssid, 32);
            if (!length) continue;
            bool duplicate = false;
            for (uint16_t j = 0; j < visible; ++j) {
                if (networks[j].authmode == networks[i].authmode &&
                    !memcmp(networks[j].ssid, networks[i].ssid, sizeof(networks[i].ssid))) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) networks[visible++] = networks[i];
        }
        printf("Redes disponibles: %u (hasta %u puntos de acceso por escaneo)\n", visible, limit);
        for (uint16_t i = 0; i < visible; ++i) {
            // Never interpret control characters supplied by an access point.
            printf("%2u) ", i + 1);
            for (size_t j = 0; j < 32 && networks[i].ssid[j]; ++j) {
                uint8_t byte = networks[i].ssid[j];
                putchar(byte < 32 || byte == 127 ? '?' : byte);
            }
            printf("  [%s, %d dBm]\n", network_security(networks[i].authmode), networks[i].rssi);
        }
        for (;;) {
            char choice[16] = {0};
            printf("Selecciona red (1-%u; r reescanea; m SSID manual; 0 o ESC cancela): ", visible);
            fflush(stdout);
            int length = read_line(choice, sizeof(choice), false, true);
            if (length < 0 || !strcmp(choice, "0")) goto done;
            if (!strcmp(choice, "r") || !strcmp(choice, "R")) break;
            if (!strcmp(choice, "m") || !strcmp(choice, "M")) {
                char ssid[33] = {0};
                printf("SSID manual (oculto, max 32 bytes; ESC cancela): "); fflush(stdout);
                int ssid_len = read_line(ssid, sizeof(ssid), true, true);
                if (ssid_len > 0) {
                    station->ssid_len = ssid_len;
                    memcpy(station->ssid, ssid, ssid_len);
                    *requires_password = false; // Security is not known for a hidden SSID.
                    selected = true;
                }
                wipe(ssid, sizeof(ssid));
                goto done;
            }
            char *end;
            errno = 0;
            long number = strtol(choice, &end, 10);
            while (*end == ' ') ++end;
            if (length == 0 || errno || end == choice || *end || number < 1 || number > visible) {
                printf("Numero no valido. Elige una red de la lista.\n");
                continue;
            }
            const wifi_ap_record_t *network = &networks[number - 1];
            station->ssid_len = strnlen((const char *)network->ssid, 32);
            memcpy(station->ssid, network->ssid, station->ssid_len);
            *requires_password = network->authmode != WIFI_AUTH_OPEN;
            selected = true;
            printf("Red %ld seleccionada (%s).\n", number, network_security(network->authmode));
            goto done;
        }
    }
done:
    wipe(networks, limit * sizeof(*networks));
    free(networks);
    return selected;
}

static void configure_station(void)
{
    char password[65] = {0};
    saved_station_t station = {.version = 1};
    bool requires_password = false;
    if (!select_network(&station, &requires_password)) {
        ESP_LOGW(TAG, "Configuracion cancelada; NVS sin cambios");
        wipe(&station, sizeof(station));
        return;
    }
    printf("Clave (oculta, 8-63 bytes, 64 hex, o vacia para red abierta): "); fflush(stdout);
    int password_len = read_line(password, sizeof(password), true, true);
    char ssid[33]={0};
    memcpy(ssid,station.ssid,station.ssid_len);
    if (password_len >= 0 && panel_station_valid(ssid,password,requires_password)) {
        station.password_len=password_len;
        memcpy(station.password,password,password_len);
        esp_err_t error=save_station_and_connect(&station);
        if (error!=ESP_OK) ESP_LOGW(TAG,"WiFi guardado/conexion: %s",esp_err_to_name(error));
        else check_connectivity();
    } else ESP_LOGW(TAG,"Configuracion cancelada o longitud/formato invalido; NVS sin cambios");
    wipe(ssid,sizeof(ssid));
    wipe(password, sizeof(password));
    wipe(&station, sizeof(station));
}

static void print_help(serial_menu_t menu)
{
    printf("\nComandos: config | status | scan | connect | check\n"
           "          reconnect-test | forget | moonraker | diag on | diag off | help\n"
           "Panel: panel help. Audio: audio help (tono manual; silencio global).\n"
           "moonraker (I): host/IP y puerto; moonraker set HOST PUERTO.\n"
           "M/P/A/W/K/D/L=menus, H=help. W ahora abre el menu WiFi; config configura.\n"
           "Tambien se acepta el prefijo wifi: wifi config, wifi status, etc.\n"
           "config escanea, muestra una lista numerada y pide la clave oculta.\n"
           "La red elegida se guarda en el P4 y conecta; m permite SSID manual.\n"
           "Diagnosticos periodicos GUI: apagados por defecto; diag on los habilita.\n");
    serial_menu_print(menu);
    fflush(stdout);
}

static void gui_diagnostics(bool enabled)
{
    esp_log_level_t level = enabled ? ESP_LOG_INFO : ESP_LOG_WARN;
    esp_log_level_set("gui_port", level);
    esp_log_level_set("gui_touch", level);
    esp_log_level_set("touch_test", level);
}

static void scan_networks(void)
{
    wifi_scan_config_t config = {.show_hidden = true};
    esp_err_t error = esp_wifi_scan_start(&config, true);
    uint16_t count = 0;
    if (error == ESP_OK) error = esp_wifi_scan_get_ap_num(&count);
    esp_wifi_clear_ap_list();
    ESP_LOGI(TAG, "SCAN_RESULT status=%s redes=%u; SSID/BSSID omitidos", esp_err_to_name(error), count);
}

static void forget_station(void)
{
    nvs_handle_t handle;
    esp_err_t error = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle);
    if (error == ESP_OK) {
        error = nvs_erase_key(handle, WIFI_KEY);
        if (error == ESP_ERR_NVS_NOT_FOUND) error = ESP_OK;
        if (error == ESP_OK) error = nvs_commit(handle);
        nvs_close(handle);
    }
    if (error == ESP_OK) {
        s_want_connection = s_saved = false;
        esp_wifi_disconnect();
        wifi_config_t empty = {0};
        esp_wifi_set_config(WIFI_IF_STA, &empty);
        ESP_LOGI(TAG, "WIFI_CONFIG_FORGOTTEN; solo se elimino la clave station_v1 de jc_wifi");
    } else ESP_LOGE(TAG, "wifi forget: %s", esp_err_to_name(error));
}

static void wifi_connection_run(void)
{
    serial_menu_t menu = SERIAL_MENU_MAIN;
    // Keep the interactive prompt readable. Errors remain visible.
    gui_diagnostics(false);
    usb_serial_jtag_driver_config_t serial_config = {.tx_buffer_size = 4096, .rx_buffer_size = 256};
    if (!usb_serial_jtag_is_driver_installed()) {
        esp_err_t error = usb_serial_jtag_driver_install(&serial_config);
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "Consola USB: %s", esp_err_to_name(error));
            set_error("Servicio de red no disponible: consola USB");
            return;
        }
    }
    usb_serial_jtag_vfs_use_driver();
    saved_station_t station = {0};
    esp_err_t loaded = load_station(&station);
    if (loaded == ESP_OK && s_snapshot.wifi_ready) {
        esp_err_t applied = apply_station(&station);
        wipe(&station, sizeof(station));
        if (applied == ESP_OK) {
            s_saved = true;
            ESP_LOGI(TAG, "WIFI_CONFIG_LOADED desde NVS del P4; contenido omitido");
            s_want_connection = true;
            connect_station();
            if (xEventGroupGetBits(s_events) & GOT_IP) check_connectivity();
        } else ESP_LOGE(TAG, "Configuracion guardada no aplicada: %s", esp_err_to_name(applied));
    } else {
        wipe(&station, sizeof(station));
        ESP_LOGI(TAG, "WIFI_SETUP_REQUIRED; no hay configuracion utilizable (%s)", esp_err_to_name(loaded));
    }
    portENTER_CRITICAL(&s_snapshot_lock);s_settings_status.ready=true;portEXIT_CRITICAL(&s_snapshot_lock);
    serial_menu_print(menu);
    ESP_LOGI(TAG, "WIFI_CONSOLE_READY");
    printf("%s", serial_menu_prompt(menu)); fflush(stdout);
    for (;;) {
        char command[128] = {0};
        int length = read_line(command, sizeof(command), false, false);
        if (length == LINE_IDLE) {
            reconnect_if_needed();
            continue;
        }
        if (length >= 0) {
            console_active(true);
            const char *action = serial_menu_normalise(command);
            serial_menu_result_t navigation = serial_menu_route(&menu, action);
            if (navigation.action == SERIAL_MENU_SHOW) {
                if(navigation.text)extension_command(navigation.text);
                serial_menu_print(menu);
                goto prompt;
            }
            if (navigation.action == SERIAL_MENU_NOTICE) {
                printf("%s\n", navigation.text);
                goto prompt;
            }
            if (navigation.action == SERIAL_MENU_INVALID) {
                printf("Opcion no valida. Enter muestra el menu actual; M vuelve al principal.\n");
                goto prompt;
            }
            if (navigation.action == SERIAL_MENU_CONFIRM_FORGET) {
                if (!s_snapshot.wifi_ready) {
                    printf("WiFi no disponible; revisar arranque Hosted.\n");
                    goto prompt;
                }
                char confirmation[8] = {0};
                printf("Borrar la red WiFi guardada? Escriba SI + Enter; otra entrada o ESC cancela: ");
                fflush(stdout);
                int confirmed = read_line(confirmation, sizeof(confirmation), false, true);
                bool forget = confirmed == 2 &&
                    (confirmation[0] == 's' || confirmation[0] == 'S') &&
                    (confirmation[1] == 'i' || confirmation[1] == 'I');
                wipe(confirmation, sizeof(confirmation));
                if (!forget) {
                    printf("Borrado cancelado; configuracion conservada.\n");
                    goto prompt;
                }
                action = "forget";
            } else if (navigation.action == SERIAL_MENU_COMMAND) {
                action = navigation.text;
            }
            if (!strncmp(action,"moonraker set ",14)) {
                char host[64],port_text[8],extra;uint16_t port;
                if(sscanf(action+14,"%63s %7s %c",host,port_text,&extra)==2 &&
                   panel_host_valid(host) && panel_port_parse(port_text,&port)) {
                    esp_err_t saved=store_endpoint(host,port);
                    printf("Moonraker: %s\n",esp_err_to_name(saved));
                }else printf("Uso: moonraker set HOST PUERTO (sin URL/ruta)\n");
            }
            else if (!strcmp(action,"moonraker") || !strcmp(action,"i")) configure_moonraker();
            else if (!strcmp(action,"status")) {
                device_network_snapshot_t snapshot; device_network_snapshot(&snapshot);
                printf("Panel: WiFi listo=%d IP=%s Moonraker=%s puerto=%u brillo=%u timeout=%u min\n",
                       snapshot.wifi_ready,snapshot.has_ip?snapshot.ip:"sin IP",
                       snapshot.moonraker_host[0]?snapshot.moonraker_host:"SIN CONFIGURAR",
                       snapshot.moonraker_port,snapshot.brightness,snapshot.sleep_minutes);
                printf("Red guardada=%d; detalle=%s\n",s_saved,
                       snapshot.error[0]?snapshot.error:"sin error");
            }
            else if (!strcmp(action,"help") || !strcmp(action,"h")) print_help(menu);
            else if (!strcmp(action, "diag on")) {
                gui_diagnostics(true);
                printf("Diagnosticos periodicos GUI activados.\n");
            } else if (!strcmp(action, "diag off")) {
                gui_diagnostics(false);
                printf("Diagnosticos periodicos GUI desactivados.\n");
            }
            else if (!s_snapshot.wifi_ready) {
                if (!extension_command(action)) ESP_LOGW(TAG,"WiFi no disponible; revisar arranque Hosted");
            }
            else if (!strcmp(action, "config")) configure_station();
            else if (!strcmp(action, "scan")) scan_networks();
            else if (!strcmp(action, "connect")) {
                if (!s_saved) ESP_LOGW(TAG, "Primero usar wifi config");
                else {
                    s_want_connection = true;
                    connect_station();
                    if (xEventGroupGetBits(s_events) & GOT_IP) check_connectivity();
                }
            } else if (!strcmp(action, "check")) check_connectivity();
            else if (!strcmp(action, "reconnect-test")) {
                if (!(xEventGroupGetBits(s_events) & GOT_IP)) ESP_LOGW(TAG, "Se requiere IP antes de probar reconexion");
                else {
                    ESP_LOGI(TAG, "RECONNECT_TEST_BEGIN; desconexion STA solicitada por usuario");
                    esp_wifi_disconnect();
                    xEventGroupWaitBits(s_events, DISCONNECTED, pdFALSE, pdFALSE, pdMS_TO_TICKS(5000));
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    s_want_connection = true;
                    connect_station();
                    if (xEventGroupGetBits(s_events) & GOT_IP) check_connectivity();
                    ESP_LOGI(TAG, "RECONNECT_TEST_END ip=%d", !!(xEventGroupGetBits(s_events) & GOT_IP));
                }
            } else if (!strcmp(action, "forget")) forget_station();
            else if (!extension_command(action))
                ESP_LOGW(TAG, "Comando desconocido; usar M para el menu o help para ayuda");
        } else if (length == LINE_TOO_LONG) {
            printf("Entrada demasiado larga; no se ejecuto ningun comando.\n");
        }
prompt:
        console_active(false);
        wipe(command, sizeof(command));
        printf("%s", serial_menu_prompt(menu)); fflush(stdout);
    }
}

// Panel settings are an independent, versioned blob; the station blob is unchanged.
typedef struct {
    uint8_t version;
    uint8_t brightness;
    uint16_t sleep_minutes;
    uint16_t port;
    char host[64];
} panel_settings_t;
static SemaphoreHandle_t s_settings_mutex;

static esp_err_t persist_panel(const panel_settings_t *settings)
{
    nvs_handle_t handle;
    esp_err_t error=nvs_open("jc_panel",NVS_READWRITE,&handle);
    if(error!=ESP_OK)return error;
    error=nvs_set_blob(handle,"settings_v1",settings,sizeof(*settings));
    if(error==ESP_OK)error=nvs_commit(handle);
    nvs_close(handle);
    return error;
}
static panel_settings_t current_panel(void)
{
    device_network_snapshot_t snapshot;device_network_snapshot(&snapshot);
    panel_settings_t settings={.version=1,.brightness=snapshot.brightness,
        .sleep_minutes=snapshot.sleep_minutes,.port=snapshot.moonraker_port};
    memcpy(settings.host,snapshot.moonraker_host,sizeof(settings.host));
    return settings;
}
static void load_panel(void)
{
    nvs_handle_t handle;
    if(nvs_open("jc_panel",NVS_READONLY,&handle)!=ESP_OK)return;
    panel_settings_t settings={0};size_t size=sizeof(settings);
    esp_err_t error=nvs_get_blob(handle,"settings_v1",&settings,&size);nvs_close(handle);
    if(error!=ESP_OK || size!=sizeof(settings) || settings.version!=1 ||
       settings.brightness>100 || settings.sleep_minutes>120 || !settings.port ||
       !memchr(settings.host,0,sizeof(settings.host)) ||
       (settings.host[0] && !panel_host_valid(settings.host)))return;
    portENTER_CRITICAL(&s_snapshot_lock);
    s_snapshot.brightness=settings.brightness;s_snapshot.sleep_minutes=settings.sleep_minutes;
    s_snapshot.moonraker_port=settings.port;
    memcpy(s_snapshot.moonraker_host,settings.host,sizeof(settings.host));
    s_snapshot.endpoint_generation=settings.host[0]?1:0;
    portEXIT_CRITICAL(&s_snapshot_lock);
}
esp_err_t device_network_set_display(uint8_t brightness,uint16_t sleep_minutes)
{
    if(brightness>100 || sleep_minutes>120)return ESP_ERR_INVALID_ARG;
    if(!s_settings_mutex)return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_settings_mutex,portMAX_DELAY);
    panel_settings_t settings=current_panel();settings.brightness=brightness;settings.sleep_minutes=sleep_minutes;
    esp_err_t error=persist_panel(&settings);
    if(error==ESP_OK){
        portENTER_CRITICAL(&s_snapshot_lock);
        s_snapshot.brightness=brightness;s_snapshot.sleep_minutes=sleep_minutes;
        portEXIT_CRITICAL(&s_snapshot_lock);
    }
    xSemaphoreGive(s_settings_mutex);
    return error;
}
static void configure_moonraker(void)
{
    char host[64]={0},port_text[8]={0};uint16_t port=7125;
    printf("Host/IP Moonraker (sin protocolo, puerto ni ruta; ESC cancela): ");fflush(stdout);
    if(read_line(host,sizeof(host),false,true)<=0 || !panel_host_valid(host)){
        printf("Host invalido/cancelado; configuracion conservada.\n");return;
    }
    printf("Puerto [7125] (1-65535; Enter usa 7125): ");fflush(stdout);
    int length=read_line(port_text,sizeof(port_text),false,true);
    if(length<0 || (length>0 && !panel_port_parse(port_text,&port))){
        printf("Puerto invalido/cancelado; configuracion conservada.\n");return;
    }
    esp_err_t error=store_endpoint(host,port);
    if(error!=ESP_OK)printf("No se guardo Moonraker: %s\n",esp_err_to_name(error));
}
static esp_err_t store_endpoint(const char *host,uint16_t port)
{
    if(!panel_host_valid(host) || !port)return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_settings_mutex,portMAX_DELAY);
    panel_settings_t settings=current_panel();snprintf(settings.host,sizeof(settings.host),"%s",host);settings.port=port;
    esp_err_t error=persist_panel(&settings);
    if(error==ESP_OK){
        portENTER_CRITICAL(&s_snapshot_lock);
        bool changed=strcmp(s_snapshot.moonraker_host,host)!=0 || s_snapshot.moonraker_port!=port;
        snprintf(s_snapshot.moonraker_host,sizeof(s_snapshot.moonraker_host),"%s",host);
        s_snapshot.moonraker_port=port;if(changed)++s_snapshot.endpoint_generation;
        portEXIT_CRITICAL(&s_snapshot_lock);
        printf("MOONRAKER_SAVED host=%s port=%u\n",host,port);
    }
    xSemaphoreGive(s_settings_mutex);
    return error;
}

static esp_err_t save_station_and_connect(const saved_station_t *station)
{
    // The console and async UI both execute here on the same owner task.
    // Commit first: failed NVS never replaces the running station.
    esp_err_t error=store_station(station);
    if (error!=ESP_OK) return error;
    s_saved=true;s_want_connection=false;
    esp_wifi_disconnect();
    xEventGroupClearBits(s_events,GOT_IP|ASSOCIATED);
    portENTER_CRITICAL(&s_snapshot_lock);
    s_snapshot.has_ip=false;s_snapshot.ip[0]=0;
    portEXIT_CRITICAL(&s_snapshot_lock);
    vTaskDelay(pdMS_TO_TICKS(500));
    error=apply_station(station);
    if (error!=ESP_OK) return error;
    s_want_connection=true;
    return connect_station()?ESP_OK:ESP_ERR_TIMEOUT;
}
typedef struct { char body[2048];size_t used;bool overflow; } endpoint_response_t;
static esp_err_t endpoint_http_event(esp_http_client_event_t *event)
{
    if (event->event_id==HTTP_EVENT_ON_DATA && event->data_len>0) {
        endpoint_response_t *response=event->user_data;
        size_t length=(size_t)event->data_len;
        if (length>=sizeof(response->body)-response->used) { response->overflow=true;return ESP_FAIL; }
        memcpy(response->body+response->used,event->data,length);
        response->used+=length;response->body[response->used]=0;
    }
    return ESP_OK;
}
static esp_err_t test_endpoint(const char *host,uint16_t port)
{
    device_network_snapshot_t network;device_network_snapshot(&network);
    if (!network.has_ip) return ESP_ERR_INVALID_STATE;
    endpoint_response_t *response=calloc(1,sizeof(*response));
    if (!response) return ESP_ERR_NO_MEM;
    char url[112];snprintf(url,sizeof(url),"http://%s:%u/server/info",host,port);
    esp_http_client_config_t config={.url=url,.timeout_ms=4000,.event_handler=endpoint_http_event,
        .user_data=response,.disable_auto_redirect=true,.buffer_size=1024};
    esp_http_client_handle_t client=esp_http_client_init(&config);
    esp_err_t error=ESP_ERR_NO_MEM;
    if (client) {
        error=esp_http_client_perform(client);
        if (error==ESP_OK && esp_http_client_get_status_code(client)!=200) error=ESP_ERR_INVALID_RESPONSE;
        if (error==ESP_OK) {
            cJSON *json=response->overflow?NULL:cJSON_Parse(response->body);
            cJSON *result=cJSON_GetObjectItemCaseSensitive(json,"result");
            // A random HTTP server returning 200 is not a successful Moonraker test.
            if (!cJSON_IsObject(result) || !cJSON_IsString(cJSON_GetObjectItemCaseSensitive(result,"moonraker_version")))
                error=ESP_ERR_INVALID_RESPONSE;
            cJSON_Delete(json);
        }
        esp_http_client_cleanup(client);
    }
    wipe(response,sizeof(*response));free(response);
    return error;
}
static void settings_process_request(void)
{
    settings_request_t request;
    if (!s_settings_queue || xQueueReceive(s_settings_queue,&request,0)!=pdTRUE) return;
    // Erase the queue's copied PSK while no new submit can enter (busy stays true).
    settings_request_t empty={0};
    xQueueOverwrite(s_settings_queue,&empty);
    xQueueReceive(s_settings_queue,&empty,0);
    esp_err_t error=ESP_OK;
    const char *message="Listo";
    if (request.operation==DEVICE_NETWORK_OP_SCAN) {
        wifi_ap_record_t *records=calloc(DEVICE_NETWORK_SCAN_MAX,sizeof(*records));
        uint16_t count=DEVICE_NETWORK_SCAN_MAX;
        if (!records) error=ESP_ERR_NO_MEM;
        else {
            wifi_scan_config_t config={.show_hidden=false};
            error=esp_wifi_scan_start(&config,true);
            if (error==ESP_OK) error=esp_wifi_scan_get_ap_records(&count,records);
            esp_wifi_clear_ap_list();
            portENTER_CRITICAL(&s_snapshot_lock);
            s_settings_status.count=0;
            if (error==ESP_OK) for (unsigned i=0;i<count;++i) {
                if (!records[i].ssid[0]) continue;
                unsigned j;
                for (j=0;j<s_settings_status.count;++j)
                    if (!strncmp(s_settings_status.networks[j].ssid,(const char *)records[i].ssid,32) &&
                        s_settings_status.networks[j].secured==(records[i].authmode!=WIFI_AUTH_OPEN)) break;
                if (j<s_settings_status.count) continue;
                device_network_ap_t *ap=&s_settings_status.networks[s_settings_status.count++];
                memcpy(ap->ssid,records[i].ssid,32);ap->ssid[32]=0;
                ap->rssi=records[i].rssi;ap->secured=records[i].authmode!=WIFI_AUTH_OPEN;
            }
            portEXIT_CRITICAL(&s_snapshot_lock);
            free(records);
        }
        message=error==ESP_OK?"Escaneo terminado; selecciona una red":"No se pudo escanear; intenta de nuevo";
    } else if (request.operation==DEVICE_NETWORK_OP_WIFI_SAVE) {
        error=save_station_and_connect(&request.station);
        message=error==ESP_OK?"WiFi guardado y conectado":
            error==ESP_ERR_TIMEOUT?"WiFi guardado; sin IP, reintentando":"No se pudo guardar/aplicar WiFi";
    } else if (request.operation==DEVICE_NETWORK_OP_ENDPOINT_SAVE) {
        error=store_endpoint(request.host,request.port);
        message=error==ESP_OK?"Moonraker guardado":"No se pudo guardar Moonraker";
    } else if (request.operation==DEVICE_NETWORK_OP_ENDPOINT_TEST) {
        error=test_endpoint(request.host,request.port);
        message=error==ESP_OK?"Moonraker responde correctamente":
            error==ESP_ERR_INVALID_STATE?"Se necesita una conexion WiFi con IP":"Moonraker no responde o requiere autorizacion";
    }
    wipe(&request,sizeof(request));
    portENTER_CRITICAL(&s_snapshot_lock);
    s_settings_status.result=error;s_settings_status.busy=false;
    snprintf(s_settings_status.message,sizeof(s_settings_status.message),"%s",message);
    ++s_settings_status.revision;
    portEXIT_CRITICAL(&s_snapshot_lock);
}

static void reconnect_if_needed(void)
{
    if(!s_snapshot.wifi_ready || !s_events || !s_want_connection ||
       (xEventGroupGetBits(s_events)&GOT_IP) || esp_timer_get_time()<s_next_reconnect)return;
    // Intention survives all failed attempts, including a prolonged AP outage.
    ESP_LOGI(TAG,"AUTO_RECONNECT (worker; GUI independiente)");
    connect_station();
    s_next_reconnect=esp_timer_get_time()+15000000;
}
static bool network_checked(esp_err_t result,const char *operation)
{
    if(result==ESP_OK)return true;
    char message[128];snprintf(message,sizeof(message),"%s: %s",operation,esp_err_to_name(result));
    set_error(message);ESP_LOGE(TAG,"%s; GUI continua",message);return false;
}
#if !CONFIG_ESP32P4_SELECTS_REV_LESS_V3 || CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ != 360
#error "Conservar perfil P4 pre-v3 360MHz"
#endif
#if !CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE || CONFIG_ESP_HOSTED_SDIO_SLOT != 1 || CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ != 5000
#error "Se requiere Hosted SDIO1 a 5MHz"
#endif
#if CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE != 54 || CONFIG_ESP_HOSTED_SDIO_PIN_CLK != 18 || CONFIG_ESP_HOSTED_SDIO_PIN_CMD != 19 || CONFIG_ESP_HOSTED_SDIO_PIN_D0 != 14 || CONFIG_ESP_HOSTED_SDIO_PIN_D1 != 15 || CONFIG_ESP_HOSTED_SDIO_PIN_D2 != 16 || CONFIG_ESP_HOSTED_SDIO_PIN_D3 != 17
#error "Pinout Hosted distinto del perfil JC4880P443 comprobado"
#endif
#if CONFIG_ESP_HOSTED_TRANSPORT_RESTART_ON_FAILURE
#error "No reiniciar P4 por fallo de transporte Hosted"
#endif
static void network_worker(void *argument)
{
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS(4000));
    ESP_LOGI(TAG,"Inicio Hosted2.12.13 remoto1.6.5 diferido; SDIO1 4bits 5MHz; sin escribir C6");
    if(!network_checked(esp_netif_init(),"netif"))goto console;
    esp_err_t loop=esp_event_loop_create_default();
    if(loop!=ESP_ERR_INVALID_STATE && !network_checked(loop,"event loop"))goto console;
    if(!network_checked(wifi_connection_init(),"STA eventos"))goto console;
    if(!network_checked(esp_hosted_init(),"hosted init") ||
       !network_checked(esp_hosted_connect_to_slave(),"hosted connect"))goto console;
    esp_hosted_coprocessor_fwver_t version={0};
    esp_err_t version_result=esp_hosted_get_coprocessor_fwversion(&version);
    if(version_result==ESP_OK){
        ESP_LOGI(TAG,"C6_FW_VERSION=%lu.%lu.%lu",(unsigned long)version.major1,
            (unsigned long)version.minor1,(unsigned long)version.patch1);
        if(version.major1!=2 || version.minor1!=12 || version.patch1!=13)
            ESP_LOGW(TAG,"Version C6 distinta; no se actualiza automaticamente");
    }else ESP_LOGW(TAG,"Version C6 no disponible: %s",esp_err_to_name(version_result));
    wifi_init_config_t config=WIFI_INIT_CONFIG_DEFAULT();
    if(!network_checked(esp_wifi_init(&config),"WiFi RPC") ||
       !network_checked(esp_wifi_set_storage(WIFI_STORAGE_RAM),"WiFi RAM") ||
       !network_checked(esp_wifi_set_mode(WIFI_MODE_STA),"WiFi STA") ||
       !network_checked(esp_wifi_start(),"WiFi start") ||
       !network_checked(esp_wifi_set_ps(WIFI_PS_NONE),"WiFi PS"))goto console;
    portENTER_CRITICAL(&s_snapshot_lock);s_snapshot.wifi_ready=true;portEXIT_CRITICAL(&s_snapshot_lock);
console:
    wifi_connection_run();
    vTaskDelete(NULL);
}
esp_err_t device_network_start(void)
{
    if(s_started)return ESP_ERR_INVALID_STATE;
    // Never erase an existing/incompatible/full NVS partition automatically.
    esp_err_t error=nvs_flash_init();
    if(error!=ESP_OK){set_error(esp_err_to_name(error));return error;}
    s_settings_mutex=xSemaphoreCreateMutex();if(!s_settings_mutex)return ESP_ERR_NO_MEM;
    s_settings_queue=xQueueCreate(1,sizeof(settings_request_t));
    if (!s_settings_queue) { vSemaphoreDelete(s_settings_mutex);s_settings_mutex=NULL;return ESP_ERR_NO_MEM; }
    load_panel();
    if(!s_snapshot.moonraker_host[0])ESP_LOGI(TAG,"MOONRAKER_SETUP_REQUIRED: consola moonraker o I");
    if(xTaskCreate(network_worker,"device_network",10240,NULL,3,NULL)!=pdPASS){
        vQueueDelete(s_settings_queue);s_settings_queue=NULL;
        vSemaphoreDelete(s_settings_mutex);s_settings_mutex=NULL;return ESP_ERR_NO_MEM;
    }
    s_started=true;return ESP_OK;
}
