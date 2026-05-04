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

void sound_service_init(void);
void sound_service_play(sound_id_t id);

bool sound_service_is_enabled(void);
void sound_service_set_enabled(bool enabled);
