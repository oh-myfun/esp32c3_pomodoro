#include "time_service.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "service/storage_service.h"
#include "service/sound_service.h"
#include <string.h>
#include <sys/time.h>

static const char *TAG = "TIME";

static const char *ntp_servers[] = {
    "pool.ntp.org",
    "cn.ntp.org.cn",
    "ntp.aliyun.com",
    "time.google.com",
    "time.windows.com",
};
#define NTP_SERVER_COUNT TIME_SERVICE_NTP_SERVER_COUNT

static const char *ntp_server_names[] = {
    "NTP Pool",
    "China",
    "Aliyun",
    "Google",
    "Windows",
};

static int ntp_server_index = 0;

static bool synced = false;
static int8_t tz_hours = 8;
static char ntp_server[64] = TIME_SERVICE_DEFAULT_NTP_SERVER;
static uint16_t sync_interval = TIME_SERVICE_DEFAULT_SYNC_INTERVAL_MIN;
static bool auto_sync = true;
static time_t last_sync_time = 0;

static void time_sync_notification(struct timeval *tv)
{
    ESP_LOGI(TAG, "Time synchronized");
    synced = true;
    time_t now = time(NULL);
    storage_save_time((uint64_t)now);
    last_sync_time = now;
    sound_service_play(SOUND_SYNC_DONE);
}

static void set_timezone(int8_t hours)
{
    tz_hours = hours;
    char tz_buffer[32];
    snprintf(tz_buffer, sizeof(tz_buffer), "CST%d", -(int)hours);
    setenv("TZ", tz_buffer, 1);
    tzset();
    storage_save_int(STORAGE_NAMESPACE_SETTINGS, "timezone", hours);
    ESP_LOGI(TAG, "Timezone set to UTC%+d", hours);
}

void time_service_init(void)
{
    // Load NTP server index
    int32_t stored_ntp_idx = 0;
    storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_NTP_SERVER, &stored_ntp_idx);
    if (stored_ntp_idx >= 0 && stored_ntp_idx < NTP_SERVER_COUNT) {
        ntp_server_index = (int)stored_ntp_idx;
    }

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, ntp_servers[ntp_server_index]);
    esp_sntp_setservername(1, NULL);
    esp_sntp_setservername(2, NULL);
    esp_sntp_set_time_sync_notification_cb(time_sync_notification);

    int32_t stored_tz = 8;
    storage_load_int(STORAGE_NAMESPACE_SETTINGS, "timezone", &stored_tz);
    tz_hours = (int8_t)stored_tz;

    int32_t stored_interval = TIME_SERVICE_DEFAULT_SYNC_INTERVAL_MIN;
    storage_load_int(STORAGE_NAMESPACE_SETTINGS, "ntp_interval", &stored_interval);
    sync_interval = (uint16_t)stored_interval;

    char tz_buffer[32];
    snprintf(tz_buffer, sizeof(tz_buffer), "CST%d", -(int)tz_hours);
    setenv("TZ", tz_buffer, 1);
    tzset();

    uint64_t saved_time = 0;
    if (storage_load_time(&saved_time) && saved_time > 0) {
        struct timeval tv = { .tv_sec = (time_t)saved_time, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "Loaded saved time: %llu", saved_time);
    }

    esp_sntp_init();

    ESP_LOGI(TAG, "Time service initialized: TZ=CST-%d, NTP=%s", tz_hours, ntp_server);
}

bool time_service_is_synced(void)
{
    return synced;
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

void time_service_request_sync(void)
{
    ESP_LOGI(TAG, "Requesting NTP sync (was%s synced)", synced ? "" : " not");
    esp_sntp_restart();
}

void time_service_tick(void)
{
    if (!auto_sync || sync_interval == 0) {
        return;
    }

    time_t now = time(NULL);
    if (last_sync_time == 0) {
        last_sync_time = now;
        return;
    }

    double elapsed = difftime(now, last_sync_time);
    if (elapsed >= (double)sync_interval * 60.0) {
        ESP_LOGI(TAG, "Auto re-sync triggered (interval=%d min, elapsed=%.0f s)",
                 sync_interval, elapsed);
        last_sync_time = now;
        time_service_request_sync();
    }
}

void time_service_set_timezone_offset(int hours)
{
    set_timezone((int8_t)hours);
}

int time_service_get_timezone_offset(void)
{
    return (int)tz_hours;
}

void time_service_set_ntp_server_index(int index)
{
    if (index < 0 || index >= NTP_SERVER_COUNT) return;
    ntp_server_index = index;
    strncpy(ntp_server, ntp_servers[index], sizeof(ntp_server) - 1);
    esp_sntp_setservername(0, ntp_server);
    esp_sntp_setservername(1, NULL);
    esp_sntp_setservername(2, NULL);
    storage_save_int(STORAGE_NAMESPACE_SETTINGS, KEY_NTP_SERVER, (int32_t)index);
    ESP_LOGI(TAG, "NTP server set to %s (%s)", ntp_server_names[index], ntp_servers[index]);
}

int time_service_get_ntp_server_index(void)
{
    return ntp_server_index;
}

const char* time_service_get_ntp_server_name(int index)
{
    if (index < 0 || index >= NTP_SERVER_COUNT) return "Unknown";
    return ntp_server_names[index];
}
