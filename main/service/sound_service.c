#include "sound_service.h"
#include "driver/buzzer.h"
#include "service/storage_service.h"
#include "esp_log.h"

static const char *TAG = "SOUND";

// Note frequencies (Hz)
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

// Melody data (compile-time constants)
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

static bool sound_enabled = true;

void sound_service_init(void)
{
    int32_t val = 1;
    storage_load_int(STORAGE_NAMESPACE_SETTINGS, "sound_on", &val);
    sound_enabled = (val != 0);
    ESP_LOGI(TAG, "Sound service initialized, enabled=%d", sound_enabled);
}

void sound_service_play(sound_id_t id)
{
    if (!sound_enabled) return;
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
    storage_save_int(STORAGE_NAMESPACE_SETTINGS, "sound_on", enabled ? 1 : 0);
    if (!enabled) {
        buzzer_stop();
    }
    ESP_LOGI(TAG, "Sound %s", enabled ? "enabled" : "disabled");
}
