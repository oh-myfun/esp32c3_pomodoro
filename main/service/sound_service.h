#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    SOUND_KEY_CLICK,
    SOUND_CONFIRM,
    SOUND_CANCEL,
    SOUND_SUCCESS,
    SOUND_FAIL,
    SOUND_WIFI_CONNECT,
    SOUND_WIFI_CONNECTED,
    SOUND_WIFI_FAILED,
    SOUND_SYNC_START,
    SOUND_SYNC_DONE,
    SOUND_POMO_START,
    SOUND_POMO_WORK_START,
    SOUND_POMO_BREAK_START,
    SOUND_POMO_WORK_DONE,
    SOUND_POMO_BREAK_DONE,
    SOUND_POMO_LONG_BREAK,
    SOUND_BUDDY_ATTENTION,
    SOUND_BUDDY_HAPPY,
    SOUND_BUDDY_SAD,
    SOUND_COUNT
} sound_id_t;

typedef enum {
    SND_CAT_KEY = 0,
    SND_CAT_UI,
    SND_CAT_NET,
    SND_CAT_POMO,
    SND_CAT_BUDDY,
    SND_CAT_HOUR_CHIME,
    SND_CAT_HALF_CHIME,
    SND_CAT_COUNT
} sound_category_t;

void sound_service_init(void);
void sound_service_play(sound_id_t id);

bool sound_service_is_enabled(void);
void sound_service_set_enabled(bool enabled);

/* 分类开关 */
bool sound_service_is_category_enabled(sound_category_t cat);
void sound_service_set_category_enabled(sound_category_t cat, bool on);

/* 报时（参数化触发，内部走总开关 + 分类开关） */
void sound_service_play_hour_chime(int hour12);   /* hour12: 1..12 */
void sound_service_play_half_chime(void);

/* _raw 变体：绕过总开关 / 分类开关，用于演示界面 */
void sound_service_play_raw(sound_id_t id);
void sound_service_play_hour_chime_raw(int hour12);
void sound_service_play_half_chime_raw(void);

/* 当前是否正在播放（用于演示界面更新 ▶ 标记） */
bool sound_service_is_playing(void);

/* 静默时段（start/end 取值 0..23；start==end 表示无静默） */
bool sound_service_is_quiet_hour(int hour);
void sound_service_set_quiet_range(int start, int end);
void sound_service_get_quiet_range(int *start, int *end);
