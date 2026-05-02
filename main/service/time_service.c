#include "time_service.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "service/storage_service.h"
#include <string.h>
#include <sys/time.h>

static const char *TAG = "TIME";

static bool synced = false;
static int8_t tz_hours = 8;
static int8_t tz_minutes = 0;
static char ntp_server[64] = TIME_SERVICE_DEFAULT_NTP_SERVER;
static uint16_t sync_interval = TIME_SERVICE_DEFAULT_SYNC_INTERVAL_MIN;
static bool auto_sync = true;

static void time_sync_notification(struct timeval *tv)
{
    ESP_LOGI(TAG, "Time synchronized");
    synced = true;
}

void time_service_init(void)
{
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, ntp_server);
    esp_sntp_set_time_sync_notification_cb(time_sync_notification);

    int32_t stored_tz = 8;
    storage_load_int(STORAGE_NAMESPACE_SETTINGS, "timezone", &stored_tz);
    tz_hours = (int8_t)stored_tz;

    int32_t stored_interval = TIME_SERVICE_DEFAULT_SYNC_INTERVAL_MIN;
    storage_load_int(STORAGE_NAMESPACE_SETTINGS, "ntp_interval", &stored_interval);
    sync_interval = (uint16_t)stored_interval;

    char tz_buffer[32];
    snprintf(tz_buffer, sizeof(tz_buffer), "CST-%d", tz_hours);
    setenv("TZ", tz_buffer, 1);
    tzset();

    uint64_t saved_time = 0;
    if (storage_load_time(&saved_time) && saved_time > 0) {
        struct timeval tv = { .tv_sec = (time_t)saved_time, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "Loaded saved time: %llu", saved_time);
        synced = true;
    }

    ESP_LOGI(TAG, "Time service initialized: TZ=CST-%d, NTP=%s", tz_hours, ntp_server);
}

bool time_service_sync(void)
{
    if (!auto_sync) {
        ESP_LOGW(TAG, "Auto sync disabled");
        return false;
    }

    esp_sntp_restart();
    synced = false;
    
    int retry = 0;
    while (retry < 5) {
        if (synced) {
            uint64_t now = time(NULL);
            storage_save_time(now);
            ESP_LOGI(TAG, "Time saved: %llu", now);
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry++;
    }
    
    ESP_LOGW(TAG, "NTP sync failed after retries");
    return false;
}

bool time_service_is_synced(void)
{
    return synced;
}

current_time_t time_service_get_current_time(void)
{
    current_time_t result = {0};
    result.timestamp = time(NULL);
    result.millis = esp_timer_get_time() / 1000;
    result.valid = synced || (result.timestamp > 1609459200);
    return result;
}

void time_service_set_timezone(int8_t hours, int8_t minutes)
{
    tz_hours = hours;
    tz_minutes = minutes;
    
    char tz_buffer[32];
    snprintf(tz_buffer, sizeof(tz_buffer), "CST%+d", hours);
    setenv("TZ", tz_buffer, 1);
    tzset();
    
    storage_save_int(STORAGE_NAMESPACE_SETTINGS, "timezone", hours);
    ESP_LOGI(TAG, "Timezone set to UTC%+d", hours);
}

timezone_info_t time_service_get_timezone(void)
{
    timezone_info_t info = {
        .timezone_hours = tz_hours,
        .timezone_minutes = tz_minutes,
    };
    snprintf(info.timezone_name, sizeof(info.timezone_name), "CST%+d", tz_hours);
    return info;
}

void time_service_set_ntp_server(const char *server)
{
    if (server && strlen(server) > 0) {
        strncpy(ntp_server, server, sizeof(ntp_server) - 1);
        esp_sntp_setservername(0, ntp_server);
        ESP_LOGI(TAG, "NTP server set to %s", server);
    }
}

const char* time_service_get_ntp_server(void)
{
    return ntp_server;
}

void time_service_set_sync_interval(uint16_t minutes)
{
    sync_interval = minutes;
    storage_save_int(STORAGE_NAMESPACE_SETTINGS, "ntp_interval", minutes);
    ESP_LOGI(TAG, "Sync interval set to %d minutes", minutes);
}

uint16_t time_service_get_sync_interval(void)
{
    return sync_interval;
}

void time_service_set_auto_sync(bool enable)
{
    auto_sync = enable;
    ESP_LOGI(TAG, "Auto sync %s", enable ? "enabled" : "disabled");
}

bool time_service_get_auto_sync(void)
{
    return auto_sync;
}

char* time_service_format_time(char *buffer, size_t len, const char *format)
{
    if (!buffer || len == 0) return buffer;
    
    time_t now = time(NULL);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    if (strftime(buffer, len, format ? format : "%H:%M:%S", &timeinfo) == 0) {
        buffer[0] = '\0';
    }
    
    return buffer;
}

char* time_service_format_date(char *buffer, size_t len, const char *format)
{
    if (!buffer || len == 0) return buffer;
    
    time_t now = time(NULL);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    if (strftime(buffer, len, format ? format : "%Y-%m-%d", &timeinfo) == 0) {
        buffer[0] = '\0';
    }
    
    return buffer;
}
