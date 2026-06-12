#include "sound_service.h"
#include "driver/buzzer.h"
#include "service/storage_service.h"
#include "esp_log.h"

static const char *TAG = "SOUND";

/* ---- Note frequencies (Hz) ---- */
#define NOTE_C4   262
#define NOTE_EB4  311
#define NOTE_G4   392
#define NOTE_B4   494
#define NOTE_C5   523
#define NOTE_E5   659
#define NOTE_G5   784
#define NOTE_A5   880
#define NOTE_B5   988
#define NOTE_C6   1047
#define NOTE_D6   1175
#define NOTE_E6   1319
#define NOTE_G6   1568
#define NOTE_A6   1760
#define NOTE_C7   2093
#define REST      0

/* ---- Existing melodies (unchanged) ---- */
static const buzzer_note_t mel_key_click[] = {
    {NOTE_C6, 30},
};
static const buzzer_note_t mel_confirm[] = {
    {NOTE_E5, 60}, {NOTE_G5, 60},
};
static const buzzer_note_t mel_cancel[] = {
    {NOTE_G5, 60}, {NOTE_E5, 60},
};
static const buzzer_note_t mel_success[] = {
    {NOTE_C5, 80}, {NOTE_E5, 80}, {NOTE_G5, 80},
};
static const buzzer_note_t mel_fail[] = {
    {NOTE_G4, 80}, {NOTE_EB4, 120},
};
static const buzzer_note_t mel_wifi_connect[] = {
    {NOTE_A5, 50},
};
static const buzzer_note_t mel_wifi_connected[] = {
    {NOTE_E5, 60}, {NOTE_B5, 80},
};
static const buzzer_note_t mel_wifi_failed[] = {
    {NOTE_B4, 100}, {REST, 50}, {NOTE_B4, 100},
};
static const buzzer_note_t mel_sync_start[] = {
    {NOTE_D6, 30},
};
static const buzzer_note_t mel_sync_done[] = {
    {NOTE_D6, 30}, {NOTE_A6, 50},
};
static const buzzer_note_t mel_pomo_start[] = {
    {NOTE_C5, 100}, {NOTE_E5, 100}, {NOTE_G5, 100}, {NOTE_C6, 150},
};
static const buzzer_note_t mel_pomo_work_start[] = {
    {NOTE_C5, 80}, {NOTE_E5, 80}, {NOTE_G5, 120},
};
static const buzzer_note_t mel_pomo_break_start[] = {
    {NOTE_G5, 100}, {NOTE_E5, 120},
};
static const buzzer_note_t mel_pomo_work_done[] = {
    {NOTE_G5, 150}, {NOTE_E5, 150}, {NOTE_C5, 200},
};
static const buzzer_note_t mel_pomo_break_done[] = {
    {NOTE_C5, 80}, {REST, 60}, {NOTE_C5, 80}, {REST, 60}, {NOTE_C5, 150},
};
static const buzzer_note_t mel_pomo_long_break[] = {
    {NOTE_G4, 200}, {NOTE_C5, 200}, {NOTE_E5, 200}, {NOTE_G5, 300},
};
static const buzzer_note_t mel_buddy_attention[] = {
    {NOTE_A5, 100}, {REST, 80}, {NOTE_A5, 100}, {REST, 80}, {NOTE_A5, 200},
};
static const buzzer_note_t mel_buddy_happy[] = {
    {NOTE_C6, 60}, {NOTE_E6, 60}, {NOTE_G6, 60}, {NOTE_C7, 120},
};
static const buzzer_note_t mel_buddy_sad[] = {
    {NOTE_E5, 150}, {NOTE_C5, 200},
};

/* Hour chime: 每组 3 响成起伏 */
static const buzzer_note_t mel_hour_group3[] = {
    {NOTE_C6, 150}, {NOTE_E6, 150}, {NOTE_G6, 200},
};
static const buzzer_note_t mel_hour_rest[] = {
    {REST, 400},
};
/* Half chime: 单响低音 */
static const buzzer_note_t mel_half[] = {
    {NOTE_A5, 200},
};

typedef struct {
    const buzzer_note_t *notes;
    uint8_t count;
} buzzer_melody_t;

static const buzzer_melody_t melodies[SOUND_COUNT] = {
    [SOUND_KEY_CLICK]        = {mel_key_click,        1},
    [SOUND_CONFIRM]          = {mel_confirm,           2},
    [SOUND_CANCEL]           = {mel_cancel,            2},
    [SOUND_SUCCESS]          = {mel_success,           3},
    [SOUND_FAIL]             = {mel_fail,              2},
    [SOUND_WIFI_CONNECT]     = {mel_wifi_connect,      1},
    [SOUND_WIFI_CONNECTED]   = {mel_wifi_connected,    2},
    [SOUND_WIFI_FAILED]      = {mel_wifi_failed,       3},
    [SOUND_SYNC_START]       = {mel_sync_start,        1},
    [SOUND_SYNC_DONE]        = {mel_sync_done,         2},
    [SOUND_POMO_START]       = {mel_pomo_start,        4},
    [SOUND_POMO_WORK_START]  = {mel_pomo_work_start,   3},
    [SOUND_POMO_BREAK_START] = {mel_pomo_break_start,  2},
    [SOUND_POMO_WORK_DONE]   = {mel_pomo_work_done,    3},
    [SOUND_POMO_BREAK_DONE]  = {mel_pomo_break_done,   5},
    [SOUND_POMO_LONG_BREAK]  = {mel_pomo_long_break,   4},
    [SOUND_BUDDY_ATTENTION]  = {mel_buddy_attention,   5},
    [SOUND_BUDDY_HAPPY]      = {mel_buddy_happy,       4},
    [SOUND_BUDDY_SAD]        = {mel_buddy_sad,         2},
};

/* ---- Category mapping ---- */
static const sound_category_t cat_map[SOUND_COUNT] = {
    [SOUND_KEY_CLICK]        = SND_CAT_KEY,
    [SOUND_CONFIRM]          = SND_CAT_UI,
    [SOUND_CANCEL]           = SND_CAT_UI,
    [SOUND_SUCCESS]          = SND_CAT_UI,
    [SOUND_FAIL]             = SND_CAT_UI,
    [SOUND_WIFI_CONNECT]     = SND_CAT_NET,
    [SOUND_WIFI_CONNECTED]   = SND_CAT_NET,
    [SOUND_WIFI_FAILED]      = SND_CAT_NET,
    [SOUND_SYNC_START]       = SND_CAT_NET,
    [SOUND_SYNC_DONE]        = SND_CAT_NET,
    [SOUND_POMO_START]       = SND_CAT_POMO,
    [SOUND_POMO_WORK_START]  = SND_CAT_POMO,
    [SOUND_POMO_BREAK_START] = SND_CAT_POMO,
    [SOUND_POMO_WORK_DONE]   = SND_CAT_POMO,
    [SOUND_POMO_BREAK_DONE]  = SND_CAT_POMO,
    [SOUND_POMO_LONG_BREAK]  = SND_CAT_POMO,
    [SOUND_BUDDY_ATTENTION]  = SND_CAT_BUDDY,
    [SOUND_BUDDY_HAPPY]      = SND_CAT_BUDDY,
    [SOUND_BUDDY_SAD]        = SND_CAT_BUDDY,
};

/* ---- NVS keys / defaults per category ---- */
static const char *cat_keys[SND_CAT_COUNT] = {
    KEY_SND_CAT_KEY, KEY_SND_CAT_UI, KEY_SND_CAT_NET, KEY_SND_CAT_POMO,
    KEY_SND_CAT_BUDDY, KEY_SND_CAT_HOUR, KEY_SND_CAT_HALF,
};
static const bool cat_defaults[SND_CAT_COUNT] = {
    true, true, true, true, true, false, false,
};

/* ---- State ---- */
static bool sound_enabled = true;
static bool cat_enabled[SND_CAT_COUNT];
static int quiet_start = 22;
static int quiet_end   = 7;

void sound_service_init(void)
{
    int32_t v;
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_SOUND, &v)) {
        sound_enabled = (v != 0);
    }
    for (int i = 0; i < SND_CAT_COUNT; i++) {
        if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, cat_keys[i], &v)) {
            cat_enabled[i] = (v != 0);
        } else {
            cat_enabled[i] = cat_defaults[i];
        }
    }
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_QUIET_START, &v) && v >= 0 && v <= 23) {
        quiet_start = (int)v;
    }
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_QUIET_END, &v) && v >= 0 && v <= 23) {
        quiet_end = (int)v;
    }
    ESP_LOGI(TAG, "Sound init enabled=%d quiet=%d-%d",
             sound_enabled, quiet_start, quiet_end);
}

void sound_service_play(sound_id_t id)
{
    if (!sound_enabled) return;
    if (id < 0 || id >= SOUND_COUNT) return;
    sound_category_t cat = cat_map[id];
    if (!cat_enabled[cat]) return;
    const buzzer_melody_t *m = &melodies[id];
    buzzer_play_melody(m->notes, m->count);
}

void sound_service_play_raw(sound_id_t id)
{
    if (id < 0 || id >= SOUND_COUNT) return;
    const buzzer_melody_t *m = &melodies[id];
    buzzer_play_melody(m->notes, m->count);
}

bool sound_service_is_enabled(void)
{
    return sound_enabled;
}

void sound_service_set_enabled(bool enabled)
{
    sound_enabled = enabled;
    storage_save_int(STORAGE_NAMESPACE_SETTINGS, KEY_SOUND, enabled ? 1 : 0);
    if (!enabled) {
        buzzer_stop();
    }
    ESP_LOGI(TAG, "Sound %s", enabled ? "enabled" : "disabled");
}

bool sound_service_is_category_enabled(sound_category_t cat)
{
    if (cat < 0 || cat >= SND_CAT_COUNT) return false;
    return cat_enabled[cat];
}

void sound_service_set_category_enabled(sound_category_t cat, bool on)
{
    if (cat < 0 || cat >= SND_CAT_COUNT) return;
    cat_enabled[cat] = on;
    storage_save_int(STORAGE_NAMESPACE_SETTINGS, cat_keys[cat], on ? 1 : 0);
    ESP_LOGI(TAG, "Category %d = %d", cat, on);
}

static void play_hour_chime_impl(int hour12)
{
    if (hour12 < 1 || hour12 > 12) return;

    /* 动态拼装：每 3 响为一组，组间插入 rest */
    buzzer_note_t buf[16];
    int n = 0;
    int full_groups = hour12 / 3;
    int remainder   = hour12 % 3;
    for (int g = 0; g < full_groups; g++) {
        if (g > 0) buf[n++] = mel_hour_rest[0];
        for (int i = 0; i < 3; i++) buf[n++] = mel_hour_group3[i];
    }
    if (remainder > 0) {
        if (full_groups > 0) buf[n++] = mel_hour_rest[0];
        for (int i = 0; i < remainder; i++) buf[n++] = mel_hour_group3[i];
    }
    buzzer_play_melody(buf, (uint8_t)n);
}

static void play_half_chime_impl(void)
{
    buzzer_play_melody(mel_half, 1);
}

void sound_service_play_hour_chime(int hour12)
{
    if (!sound_enabled) return;
    if (!cat_enabled[SND_CAT_HOUR_CHIME]) return;
    play_hour_chime_impl(hour12);
}

void sound_service_play_half_chime(void)
{
    if (!sound_enabled) return;
    if (!cat_enabled[SND_CAT_HALF_CHIME]) return;
    play_half_chime_impl();
}

void sound_service_play_hour_chime_raw(int hour12)
{
    play_hour_chime_impl(hour12);
}

void sound_service_play_half_chime_raw(void)
{
    play_half_chime_impl();
}

bool sound_service_is_quiet_hour(int hour)
{
    int s = quiet_start, e = quiet_end;
    if (s == e) return false;
    return (s < e) ? (hour >= s && hour < e)
                   : (hour >= s || hour < e);
}

void sound_service_set_quiet_range(int start, int end)
{
    if (start < 0 || start > 23) return;
    if (end < 0 || end > 23) return;
    quiet_start = start;
    quiet_end = end;
    storage_save_int(STORAGE_NAMESPACE_SETTINGS, KEY_QUIET_START, start);
    storage_save_int(STORAGE_NAMESPACE_SETTINGS, KEY_QUIET_END, end);
    ESP_LOGI(TAG, "Quiet range set: %d-%d", start, end);
}

void sound_service_get_quiet_range(int *start, int *end)
{
    if (start) *start = quiet_start;
    if (end)   *end   = quiet_end;
}
