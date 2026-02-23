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
#define KEY_COMPLETED    "completed"
#define KEY_CURRENT_CYCLE "cycle"

bool storage_save_string(const char *namespace, const char *key, const char *value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS namespace %s", namespace);
        return false;
    }

    err = nvs_set_str(handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save %s.%s", namespace, key);
        nvs_close(handle);
        return false;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

bool storage_load_string(const char *namespace, const char *key, char *value, size_t len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    err = nvs_get_str(handle, key, value, &len);
    nvs_close(handle);
    return err == ESP_OK;
}

bool storage_save_int(const char *namespace, const char *key, int32_t value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace, NVS_READWRITE, &handle);
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

bool storage_load_int(const char *namespace, const char *key, int32_t *value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace, NVS_READONLY, &handle);
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

void storage_clear_namespace(const char *namespace)
{
    nvs_handle_t handle;
    if (nvs_open(namespace, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI(TAG, "Namespace %s cleared", namespace);
    }
}
