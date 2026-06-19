#include "chime_service.h"
#include "sound_service.h"
#include "time_service.h"
#include "esp_log.h"
#include <time.h>
#include <sys/time.h>

static const char *TAG = "CHIME";

static long s_last_minute_id = -1;

void chime_service_init(void)
{
    time_t now;
    time(&now);
    s_last_minute_id = (long)(now / 60);
    ESP_LOGI(TAG, "Chime service init, current minute_id=%ld", s_last_minute_id);
}

void chime_service_tick(void)
{
    time_t now;
    time(&now);
    struct tm t;
    localtime_r(&now, &t);

    /* 时间合理性检查：年份 < 2024 视为时间未初始化（避免在 1970 报时）。
     * 不依赖 time_service_is_synced() —— NVS 中保存的时间也可以让报时
     * 正常工作，深度休眠后 NTP 短暂失联不影响报时。 */
    if (t.tm_year + 1900 < 2024) return;

    /* 只在整分(秒==0 附近，靠去重处理)且分钟为 0 或 30 时考虑触发 */
    if (t.tm_min != 0 && t.tm_min != 30) return;

    long mid = (long)(now / 60);
    if (mid == s_last_minute_id) return;
    s_last_minute_id = mid;

    /* 静默时段不响 */
    if (sound_service_is_quiet_hour(t.tm_hour)) return;

    if (t.tm_min == 0) {
        int h12 = t.tm_hour % 12;
        if (h12 == 0) h12 = 12;
        ESP_LOGI(TAG, "Hour chime: %d", h12);
        sound_service_play_hour_chime(h12);
    } else {
        ESP_LOGI(TAG, "Half chime");
        sound_service_play_half_chime();
    }
}
