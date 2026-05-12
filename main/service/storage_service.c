#include "storage_service.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "STORAGE";

#define KEY_WIFI_SSID     "ssid"
#define KEY_WIFI_PASSWORD "password"
#define KEY_POM_STATE     "state"
#define KEY_POM_SETTINGS  "settings"
#define KEY_WORK_MIN      "work_min"
#define KEY_BREAK_MIN     "break_min"
#define KEY_LONG_BREAK   "long_break"
#define KEY_CYCLES       "cycles"
#define KEY_COMPLETED    "completed"
#define KEY_CURRENT_CYCLE "cycle"

bool storage_save_string(const char *ns, const char *key, const char *value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS namespace %s", ns);
        return false;
    }

    err = nvs_set_str(handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save %s.%s", ns, key);
        nvs_close(handle);
        return false;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

bool storage_load_string(const char *ns, const char *key, char *value, size_t len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    err = nvs_get_str(handle, key, value, &len);
    nvs_close(handle);
    return err == ESP_OK;
}

bool storage_save_int(const char *ns, const char *key, int32_t value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return false;
    }

    err = nvs_set_i32(handle, key, value);
    if (err != ESP_OK) {
        nvs_close(handle);
        return false;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

bool storage_load_int(const char *ns, const char *key, int32_t *value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    err = nvs_get_i32(handle, key, value);
    nvs_close(handle);
    return err == ESP_OK;
}

bool storage_save_wifi_config(const char *ssid, const char *password)
{
    bool result = storage_save_string(STORAGE_NAMESPACE_WIFI, KEY_WIFI_SSID, ssid);
    if (password && strlen(password) > 0) {
        result &= storage_save_string(STORAGE_NAMESPACE_WIFI, KEY_WIFI_PASSWORD, password);
    }
    return result;
}

bool storage_load_wifi_config(char *ssid, size_t ssid_len, char *password, size_t password_len)
{
    bool ssid_ok = storage_load_string(STORAGE_NAMESPACE_WIFI, KEY_WIFI_SSID, ssid, ssid_len);
    storage_load_string(STORAGE_NAMESPACE_WIFI, KEY_WIFI_PASSWORD, password, password_len);
    return ssid_ok;
}

bool storage_save_pomodoro_settings(void *settings_ptr)
{
    if (!settings_ptr) return false;
    int32_t *data = (int32_t *)settings_ptr;
    
    bool result = true;
    result &= storage_save_int(STORAGE_NAMESPACE_POMODORO, KEY_WORK_MIN, data[0]);
    result &= storage_save_int(STORAGE_NAMESPACE_POMODORO, KEY_BREAK_MIN, data[1]);
    result &= storage_save_int(STORAGE_NAMESPACE_POMODORO, KEY_LONG_BREAK, data[2]);
    result &= storage_save_int(STORAGE_NAMESPACE_POMODORO, KEY_CYCLES, data[3]);
    return result;
}

bool storage_load_pomodoro_settings(void *settings_ptr)
{
    if (!settings_ptr) return false;
    int32_t *data = (int32_t *)settings_ptr;
    
    int32_t val;
    if (storage_load_int(STORAGE_NAMESPACE_POMODORO, KEY_WORK_MIN, &val)) data[0] = val;
    else data[0] = 25;
    
    if (storage_load_int(STORAGE_NAMESPACE_POMODORO, KEY_BREAK_MIN, &val)) data[1] = val;
    else data[1] = 5;
    
    if (storage_load_int(STORAGE_NAMESPACE_POMODORO, KEY_LONG_BREAK, &val)) data[2] = val;
    else data[2] = 15;
    
    if (storage_load_int(STORAGE_NAMESPACE_POMODORO, KEY_CYCLES, &val)) data[3] = val;
    else data[3] = 4;
    
    return true;
}

bool storage_save_pomodoro_state(void *state_ptr)
{
    if (!state_ptr) return false;
    int32_t *data = (int32_t *)state_ptr;
    
    bool result = true;
    result &= storage_save_int(STORAGE_NAMESPACE_POMODORO, KEY_COMPLETED, data[0]);
    result &= storage_save_int(STORAGE_NAMESPACE_POMODORO, KEY_CURRENT_CYCLE, data[1]);
    return result;
}

bool storage_load_pomodoro_state(void *state_ptr)
{
    if (!state_ptr) return false;
    int32_t *data = (int32_t *)state_ptr;
    
    int32_t val;
    if (storage_load_int(STORAGE_NAMESPACE_POMODORO, KEY_COMPLETED, &val)) data[0] = val;
    else data[0] = 0;
    
    if (storage_load_int(STORAGE_NAMESPACE_POMODORO, KEY_CURRENT_CYCLE, &val)) data[1] = val;
    else data[1] = 0;
    
    return true;
}

bool storage_save_time(uint64_t timestamp)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE_SETTINGS, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return false;
    }
    
    int32_t high = (int32_t)(timestamp >> 32);
    int32_t low = (int32_t)(timestamp & 0xFFFFFFFF);
    
    bool result = true;
    result &= (nvs_set_i32(handle, "time_high", high) == ESP_OK);
    result &= (nvs_set_i32(handle, "time_low", low) == ESP_OK);
    result &= (nvs_commit(handle) == ESP_OK);
    
    nvs_close(handle);
    return result;
}

bool storage_load_time(uint64_t *timestamp)
{
    if (!timestamp) return false;
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE_SETTINGS, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }
    
    int32_t high = 0;
    int32_t low = 0;
    
    bool result = true;
    result &= (nvs_get_i32(handle, "time_high", &high) == ESP_OK);
    result &= (nvs_get_i32(handle, "time_low", &low) == ESP_OK);
    
    nvs_close(handle);
    
    if (result) {
        *timestamp = ((uint64_t)(uint32_t)high << 32) | (uint64_t)(uint32_t)low;
    }
    
    return result;
}

void storage_clear_namespace(const char *ns)
{
    nvs_handle_t handle;
    if (nvs_open(ns, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI(TAG, "Namespace %s cleared", ns);
    }
}

// --- WiFi multi-profile ---

#define KEY_WIFI_COUNT "count"
#define KEY_SSID_PREFIX "ssid_"
#define KEY_PWD_PREFIX  "pwd_"

static void wifi_profile_save_at(int index, const char *ssid, const char *password)
{
    char key[20];
    snprintf(key, sizeof(key), "%s%d", KEY_SSID_PREFIX, index);
    storage_save_string(STORAGE_NAMESPACE_WIFI, key, ssid);
    snprintf(key, sizeof(key), "%s%d", KEY_PWD_PREFIX, index);
    storage_save_string(STORAGE_NAMESPACE_WIFI, key, password ? password : "");
}

static void wifi_profile_shift_forward(int from, int to)
{
    // Shift entries [from..to-1] to [from-1..to-2], effectively removing slot (from-1).
    if (from >= to) return;
    for (int i = from; i < to; i++) {
        char key[20];
        char s[64] = {0}, p[64] = {0};
        snprintf(key, sizeof(key), "%s%d", KEY_SSID_PREFIX, i);
        storage_load_string(STORAGE_NAMESPACE_WIFI, key, s, sizeof(s));
        snprintf(key, sizeof(key), "%s%d", KEY_PWD_PREFIX, i);
        storage_load_string(STORAGE_NAMESPACE_WIFI, key, p, sizeof(p));
        wifi_profile_save_at(i - 1, s, p);
    }
}

static void wifi_profile_erase_slot(int index)
{
    nvs_handle_t handle;
    if (nvs_open(STORAGE_NAMESPACE_WIFI, NVS_READWRITE, &handle) != ESP_OK) return;
    char key[20];
    snprintf(key, sizeof(key), "%s%d", KEY_SSID_PREFIX, index);
    nvs_erase_key(handle, key);
    snprintf(key, sizeof(key), "%s%d", KEY_PWD_PREFIX, index);
    nvs_erase_key(handle, key);
    nvs_commit(handle);
    nvs_close(handle);
}

static bool wifi_key_exists(const char *key)
{
    nvs_handle_t handle;
    if (nvs_open(STORAGE_NAMESPACE_WIFI, NVS_READONLY, &handle) != ESP_OK) return false;
    size_t len = 0;
    esp_err_t err = nvs_get_str(handle, key, NULL, &len);
    nvs_close(handle);
    return err == ESP_OK;
}

static bool wifi_int_key_exists(const char *key)
{
    nvs_handle_t handle;
    if (nvs_open(STORAGE_NAMESPACE_WIFI, NVS_READONLY, &handle) != ESP_OK) return false;
    int32_t val;
    esp_err_t err = nvs_get_i32(handle, key, &val);
    nvs_close(handle);
    return err == ESP_OK;
}

static int wifi_profile_find_ssid(const char *ssid)
{
    char key[20], buf[64];
    for (int i = 0; i < WIFI_PROFILE_MAX; i++) {
        snprintf(key, sizeof(key), "%s%d", KEY_SSID_PREFIX, i);
        if (storage_load_string(STORAGE_NAMESPACE_WIFI, key, buf, sizeof(buf))) {
            if (strcmp(buf, ssid) == 0) return i;
        }
    }
    return -1;
}

int storage_get_wifi_profile_count(void)
{
    int32_t count = 0;
    storage_load_int(STORAGE_NAMESPACE_WIFI, KEY_WIFI_COUNT, &count);
    return (int)count;
}

void storage_add_wifi_profile(const char *ssid, const char *password)
{
    if (!ssid || ssid[0] == '\0') return;

    int count = storage_get_wifi_profile_count();

    // Check if SSID already exists; if so, remove old slot
    int existing = wifi_profile_find_ssid(ssid);
    if (existing >= 0 && existing < count) {
        wifi_profile_shift_forward(existing + 1, count);
        wifi_profile_erase_slot(count - 1);
        count--;
    }

    // If full, evict oldest (index 0)
    if (count >= WIFI_PROFILE_MAX) {
        wifi_profile_shift_forward(1, count);
        wifi_profile_erase_slot(count - 1);
        count = WIFI_PROFILE_MAX - 1;
    }

    // Append new profile at index count
    wifi_profile_save_at(count, ssid, password ? password : "");
    storage_save_int(STORAGE_NAMESPACE_WIFI, KEY_WIFI_COUNT, count + 1);
    ESP_LOGI(TAG, "WiFi profile added: %s (total %d)", ssid, count + 1);
}

bool storage_load_wifi_profile(int index, char *ssid, size_t ssid_len, char *password, size_t pwd_len)
{
    if (index < 0 || index >= WIFI_PROFILE_MAX) return false;
    char key[20];
    snprintf(key, sizeof(key), "%s%d", KEY_SSID_PREFIX, index);
    bool ok = storage_load_string(STORAGE_NAMESPACE_WIFI, key, ssid, ssid_len);
    snprintf(key, sizeof(key), "%s%d", KEY_PWD_PREFIX, index);
    storage_load_string(STORAGE_NAMESPACE_WIFI, key, password, pwd_len);
    return ok;
}

void storage_delete_wifi_profile(int index)
{
    int count = storage_get_wifi_profile_count();
    if (index < 0 || index >= count) return;

    // Shift entries [index+1..count-1] to [index..count-2]
    for (int i = index + 1; i < count; i++) {
        char key[20], s[64] = {0}, p[64] = {0};
        snprintf(key, sizeof(key), "%s%d", KEY_SSID_PREFIX, i);
        storage_load_string(STORAGE_NAMESPACE_WIFI, key, s, sizeof(s));
        snprintf(key, sizeof(key), "%s%d", KEY_PWD_PREFIX, i);
        storage_load_string(STORAGE_NAMESPACE_WIFI, key, p, sizeof(p));
        wifi_profile_save_at(i - 1, s, p);
    }

    // Erase the last slot
    wifi_profile_erase_slot(count - 1);
    storage_save_int(STORAGE_NAMESPACE_WIFI, KEY_WIFI_COUNT, count - 1);
    ESP_LOGI(TAG, "WiFi profile deleted at index %d (total %d)", index, count - 1);
}

void storage_migrate_wifi_config(void)
{
    bool has_count = wifi_int_key_exists(KEY_WIFI_COUNT);
    bool has_old_ssid = wifi_key_exists(KEY_WIFI_SSID);

    if (!has_count && has_old_ssid) {
        char ssid[64] = {0}, pwd[64] = {0};
        storage_load_string(STORAGE_NAMESPACE_WIFI, KEY_WIFI_SSID, ssid, sizeof(ssid));
        storage_load_string(STORAGE_NAMESPACE_WIFI, KEY_WIFI_PASSWORD, pwd, sizeof(pwd));

        // Save as profile 0
        wifi_profile_save_at(0, ssid, pwd);
        storage_save_int(STORAGE_NAMESPACE_WIFI, KEY_WIFI_COUNT, 1);

        // Erase old keys
        nvs_handle_t handle;
        if (nvs_open(STORAGE_NAMESPACE_WIFI, NVS_READWRITE, &handle) == ESP_OK) {
            nvs_erase_key(handle, KEY_WIFI_SSID);
            nvs_erase_key(handle, KEY_WIFI_PASSWORD);
            nvs_commit(handle);
            nvs_close(handle);
        }

        ESP_LOGI(TAG, "WiFi config migrated: %s -> profile 0", ssid);
    }
}

void storage_migrate_settings_keys(void)
{
    nvs_handle_t handle;
    if (nvs_open(STORAGE_NAMESPACE_SETTINGS, NVS_READWRITE, &handle) != ESP_OK) return;

    int32_t val;
    bool changed = false;

    // Migrate "sound_on" -> "sound"
    if (nvs_get_i32(handle, "sound_on", &val) == ESP_OK) {
        nvs_set_i32(handle, KEY_SOUND, val);
        nvs_erase_key(handle, "sound_on");
        changed = true;
        ESP_LOGI(TAG, "Migrated settings/sound_on -> sound = %ld", (long)val);
    }

    // Migrate "enc_rev" -> "enc_dir"
    if (nvs_get_i32(handle, "enc_rev", &val) == ESP_OK) {
        nvs_set_i32(handle, KEY_ENC_DIR, val);
        nvs_erase_key(handle, "enc_rev");
        changed = true;
        ESP_LOGI(TAG, "Migrated settings/enc_rev -> enc_dir = %ld", (long)val);
    }

    if (changed) {
        nvs_commit(handle);
    }
    nvs_close(handle);
}
