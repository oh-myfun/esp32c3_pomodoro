#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#define STORAGE_NAMESPACE_POMODORO "pomodoro"
#define STORAGE_NAMESPACE_SETTINGS "settings"
#define STORAGE_NAMESPACE_WIFI     "wifi"

bool storage_save_wifi_config(const char *ssid, const char *password);

bool storage_load_wifi_config(char *ssid, size_t ssid_len, char *password, size_t password_len);

bool storage_save_pomodoro_state(void *state);

bool storage_load_pomodoro_state(void *state);

bool storage_save_pomodoro_settings(void *settings);

bool storage_load_pomodoro_settings(void *settings);

bool storage_save_string(const char *ns, const char *key, const char *value);

bool storage_load_string(const char *ns, const char *key, char *value, size_t len);

bool storage_save_int(const char *ns, const char *key, int32_t value);

bool storage_load_int(const char *ns, const char *key, int32_t *value);

bool storage_save_time(uint64_t timestamp);

bool storage_load_time(uint64_t *timestamp);

void storage_clear_namespace(const char *ns);

// WiFi multi-profile storage (max 10)
#define WIFI_PROFILE_MAX 10

int  storage_get_wifi_profile_count(void);
void storage_add_wifi_profile(const char *ssid, const char *password);
bool storage_load_wifi_profile(int index, char *ssid, size_t ssid_len, char *password, size_t pwd_len);
void storage_delete_wifi_profile(int index);
void storage_migrate_wifi_config(void);
