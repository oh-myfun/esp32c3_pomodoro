#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    WIFI_STATE_DISCONNECTED = 0,
    WIFI_STATE_SCANNING,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED
} wifi_state_t;

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool open;
} wifi_ap_info_t;

typedef struct {
    void (*on_connected)(const char *ip);
    void (*on_disconnected)(void);
    void (*on_scan_complete)(int count);
    void (*on_connect_failed)(void);
} wifi_callbacks_t;

int  wifi_service_init(void);
void wifi_service_register_callbacks(const wifi_callbacks_t *cbs);
void wifi_service_scan(void);
int  wifi_service_get_scan_count(void);
const wifi_ap_info_t* wifi_service_get_ap(int index);
void wifi_service_connect(const char *ssid, const char *password);
void wifi_service_disconnect(void);
bool wifi_service_is_connected(void);
const char* wifi_service_get_connected_ssid(void);
const char* wifi_service_get_ip(void);
wifi_state_t wifi_service_get_state(void);

// Saved profile management
int         wifi_service_get_saved_count(void);
const char* wifi_service_get_saved_ssid(int index);
void        wifi_service_delete_saved(int index);
bool        wifi_service_is_saved(const char *ssid);
