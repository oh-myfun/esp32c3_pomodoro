#include "ui_helpers.h"
#include "ui_screen_pomodoro.h"
#include "i18n.h"
#include "custom_font.h"
#include "ui_manager.h"
#include "pomodoro/pomodoro_engine.h"
#include "service/sound_service.h"
#include "service/led_service.h"
#include "service/storage_service.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_POMODORO";

/* ---- Pomodoro mode helpers ---- */

static bool pomo_is_running(void)
{
    pomodoro_state_t state = pomodoro_engine_get_state();
    return state.phase != POMODORO_PHASE_IDLE && state.phase != POMODORO_PHASE_PAUSED;
}

/* ---- Display objects ---- */

static lv_obj_t *progress_arc = NULL;
static lv_obj_t *timer_label = NULL;
static lv_obj_t *phase_label = NULL;
static lv_obj_t *completed_label = NULL;
static lv_obj_t *hint_label = NULL;
static lv_obj_t *cycle_label = NULL;

static uint32_t total_seconds = 25 * 60;

/* ---- Timer (countdown alarm) mode ---- */

typedef enum {
    TIMER_IDLE = 0,   /* adjusting time with encoder */
    TIMER_RUNNING,    /* counting down */
    TIMER_PAUSED,     /* paused */
    TIMER_ALARM,      /* time's up, alarm playing */
} timer_state_t;

static bool s_timer_mode = false;
static timer_state_t s_timer_state = TIMER_IDLE;
static uint32_t s_timer_total = 5 * 60;
static uint32_t s_timer_remaining = 0;

#define TIMER_MIN_SECONDS  (1 * 60)    /* 1 min */
#define TIMER_MAX_SECONDS  (360 * 60)  /* 360 min */

#define KEY_TIMER_TOTAL "timer_total"

static void timer_set_total(uint32_t seconds)
{
    if (seconds < TIMER_MIN_SECONDS) seconds = TIMER_MIN_SECONDS;
    if (seconds > TIMER_MAX_SECONDS) seconds = TIMER_MAX_SECONDS;
    s_timer_total = seconds;
}

static void timer_load_total(void)
{
    int32_t val = 0;
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_TIMER_TOTAL, &val) && val >= 1 && val <= 360) {
        s_timer_total = (uint32_t)val * 60;
    }
}

static void timer_save_total(void)
{
    storage_save_int(STORAGE_NAMESPACE_SETTINGS, KEY_TIMER_TOTAL, (int32_t)(s_timer_total / 60));
}

/* ---- Display helpers ---- */

static void update_time_display(uint32_t remaining_seconds)
{
    if (!timer_label) return;

    uint32_t minutes = remaining_seconds / 60;
    uint32_t seconds = remaining_seconds % 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)minutes, (unsigned long)seconds);
    lv_label_set_text(timer_label, buf);

    if (progress_arc && total_seconds > 0) {
        uint32_t progress = (remaining_seconds * 360) / total_seconds;
        if (progress > 360) progress = 360;
        lv_arc_set_value(progress_arc, progress);
    }
}

static void update_timer_display(void)
{
    if (!s_timer_mode) return;

    const char *phase_text;
    uint32_t color;
    const char *hint;

    switch (s_timer_state) {
    case TIMER_RUNNING:
        phase_text = i18n(STR_PHASE_TIMER);
        color = 0xFF6B6B;
        hint = i18n(STR_H_SET_START_PAUSE_PRESS_STOP);
        break;
    case TIMER_PAUSED:
        phase_text = i18n(STR_PHASE_PAUSED);
        color = 0xFFFF00;
        hint = i18n(STR_H_SET_START_PAUSE_PRESS_STOP);
        break;
    case TIMER_ALARM:
        phase_text = i18n(STR_PHASE_ALARM);
        color = 0xFF4444;
        hint = i18n(STR_H_SET_ENTER_PRESS_BACK);
        break;
    default: /* TIMER_IDLE */
        phase_text = i18n(STR_PHASE_TIMER);
        color = 0xFFAA00;
        hint = i18n(STR_H_SET_START_PAUSE_PRESS_STOP);
        break;
    }

    if (phase_label) {
        lv_obj_set_style_text_color(phase_label, lv_color_hex(color), 0);
        lv_label_set_text(phase_label, phase_text);
    }
    if (progress_arc) {
        lv_obj_set_style_arc_color(progress_arc, lv_color_hex(color), LV_PART_INDICATOR);
    }
    if (hint_label) {
        lv_label_set_text(hint_label, hint);
    }

    uint32_t show = (s_timer_state == TIMER_IDLE) ? s_timer_total : s_timer_remaining;
    total_seconds = s_timer_total;
    update_time_display(show);

    /* Hide pomodoro-specific labels in timer mode */
    if (cycle_label) lv_label_set_text(cycle_label, "");
    if (completed_label) lv_label_set_text(completed_label, "");
}

static void update_pomo_display(void)
{
    pomodoro_settings_t settings = pomodoro_engine_get_settings();
    pomodoro_state_t state = pomodoro_engine_get_state();
    total_seconds = settings.work_minutes * 60;

    if (timer_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:00", settings.work_minutes);
        lv_label_set_text(timer_label, buf);
    }
    if (cycle_label) {
        char buf[20];
        snprintf(buf, sizeof(buf), "%u/%u", (unsigned int)state.current_cycle, settings.cycles_until_long_break);
        lv_label_set_text(cycle_label, buf);
    }
    if (completed_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "🍅x%lu", (unsigned long)state.completed_count);
        lv_label_set_text(completed_label, buf);
    }
    if (hint_label) {
        lv_label_set_text(hint_label, i18n(STR_H_SET_START_PAUSE_PRESS_STOP));
    }

    /* Restore phase display */
    uint32_t color;
    const char *phase_text;
    switch (state.phase) {
        case POMODORO_PHASE_WORK:        color = 0xFF6B6B; phase_text = i18n(STR_PHASE_WORK); total_seconds = settings.work_minutes * 60; break;
        case POMODORO_PHASE_BREAK:       color = 0x4CAF50; phase_text = i18n(STR_PHASE_BREAK); total_seconds = settings.break_minutes * 60; break;
        case POMODORO_PHASE_LONG_BREAK:  color = 0x4D96FF; phase_text = i18n(STR_PHASE_LONG_BREAK); total_seconds = settings.long_break_minutes * 60; break;
        case POMODORO_PHASE_PAUSED:      color = 0xFFFF00; phase_text = i18n(STR_PHASE_PAUSED); total_seconds = state.remaining_seconds; break;
        default:                         color = 0xAAAAAA; phase_text = i18n(STR_PHASE_IDLE); total_seconds = settings.work_minutes * 60; break;
    }
    if (phase_label) {
        lv_obj_set_style_text_color(phase_label, lv_color_hex(color), 0);
        lv_label_set_text(phase_label, phase_text);
    }
    if (progress_arc) {
        lv_obj_set_style_arc_color(progress_arc, lv_color_hex(color), LV_PART_INDICATOR);
    }
    update_time_display(state.remaining_seconds);
}

/* ---- Input callbacks ---- */

static void pomo_on_encoder_cw(void)
{
    ui_switch_screen(UI_SCREEN_BUDDY);
}

static void pomo_on_encoder_ccw(void)
{
    ui_switch_screen(UI_SCREEN_SENSOR);
}

static void pomo_on_encoder_press(void)
{
    if (s_timer_mode) {
        if (s_timer_state == TIMER_IDLE) {
            ui_push_screen(UI_SCREEN_SETTINGS_ALARM);
        } else {
            s_timer_state = TIMER_IDLE;
            s_timer_remaining = s_timer_total;
            led_service_wait_done(LED_WAIT_POMODORO);
            update_timer_display();
        }
    } else {
        pomodoro_state_t state = pomodoro_engine_get_state();
        if (state.phase == POMODORO_PHASE_IDLE) {
            ui_push_screen(UI_SCREEN_SETTINGS_POMODORO);
        } else {
            pomodoro_engine_stop();
            led_service_wait_done(LED_WAIT_POMODORO);
        }
    }
}

static void pomo_on_settings_press(void)
{
    if (s_timer_mode) {
        switch (s_timer_state) {
        case TIMER_IDLE:
            /* Start countdown */
            s_timer_remaining = s_timer_total;
            s_timer_state = TIMER_RUNNING;
            sound_service_play(SOUND_POMO_START);
            led_service_play(LED_COLOR_WORK);
            update_timer_display();
            break;
        case TIMER_RUNNING:
            /* Pause */
            s_timer_state = TIMER_PAUSED;
            sound_service_play(SOUND_CONFIRM);
            led_service_wait((led_color_t){255, 255, 0}, LED_WAIT_POMODORO);
            update_timer_display();
            break;
        case TIMER_PAUSED:
            /* Resume */
            s_timer_state = TIMER_RUNNING;
            led_service_wait_done(LED_WAIT_POMODORO);
            sound_service_play(SOUND_POMO_START);
            led_service_play(LED_COLOR_WORK);
            update_timer_display();
            break;
        case TIMER_ALARM:
            /* Dismiss alarm → back to idle, ready for next round */
            s_timer_state = TIMER_IDLE;
            led_service_wait_done(LED_WAIT_POMODORO);
            update_timer_display();
            break;
        }
    } else {
        pomodoro_state_t state = pomodoro_engine_get_state();
        if (state.phase == POMODORO_PHASE_IDLE) {
            pomodoro_engine_start();
            sound_service_play(SOUND_POMO_START);
            led_service_play(LED_COLOR_WORK);
        } else if (state.is_paused) {
            pomodoro_engine_resume();
            pomodoro_state_t resumed = pomodoro_engine_get_state();
            led_service_wait_done(LED_WAIT_POMODORO);
            if (resumed.phase == POMODORO_PHASE_WORK) {
                sound_service_play(SOUND_POMO_WORK_START);
                led_service_play(LED_COLOR_WORK);
            } else if (resumed.phase == POMODORO_PHASE_BREAK) {
                sound_service_play(SOUND_POMO_BREAK_START);
                led_service_play(LED_COLOR_BREAK);
            } else if (resumed.phase == POMODORO_PHASE_LONG_BREAK) {
                sound_service_play(SOUND_POMO_LONG_BREAK);
                led_service_play(LED_COLOR_LONG_BREAK);
            }
        } else {
            pomodoro_engine_pause();
            sound_service_play(SOUND_CONFIRM);
            led_service_wait((led_color_t){255, 255, 0}, LED_WAIT_POMODORO);
        }
    }
}

static const char *pomo_on_long_press_hint(bool top_key)
{
    if (!top_key) return NULL;
    return i18n(STR_ACT_SWITCH_MODE);
}

static void pomo_on_settings_long_press(void)
{
    /* Toggle between pomodoro and timer mode */
    s_timer_mode = !s_timer_mode;
    if (s_timer_mode) {
        /* Stop pomodoro if running */
        if (pomo_is_running()) {
            pomodoro_engine_stop();
            led_service_wait_done(LED_WAIT_POMODORO);
        }
        s_timer_state = TIMER_IDLE;
        s_timer_remaining = s_timer_total;
        sound_service_play(SOUND_CONFIRM);
        update_timer_display();
    } else {
        /* Stop timer if running */
        s_timer_state = TIMER_IDLE;
        led_service_wait_done(LED_WAIT_POMODORO);
        sound_service_play(SOUND_CONFIRM);
        update_pomo_display();
    }
    ESP_LOGI(TAG, "Mode: %s", s_timer_mode ? "Timer" : "Pomodoro");
}

/* ---- Screen creation ---- */

lv_obj_t* ui_screen_pomodoro_create(void)
{
    lv_obj_t *screen = ui_create_screen();

    cycle_label = lv_label_create(screen);
    lv_obj_set_style_text_color(cycle_label, UI_COLOR_TEXT, 0);
    lv_label_set_text(cycle_label, "1/4");
    lv_obj_set_style_text_font(cycle_label, &custom_font_16, 0);
    lv_obj_align(cycle_label, LV_ALIGN_TOP_LEFT, 10, 8);

    phase_label = lv_label_create(screen);
    lv_obj_set_style_text_color(phase_label, UI_COLOR_WARN, 0);
    lv_label_set_text(phase_label, i18n(STR_PHASE_WORK));
    lv_obj_set_style_text_font(phase_label, &custom_font_16, 0);
    lv_obj_align(phase_label, LV_ALIGN_TOP_MID, 0, 8);

    completed_label = lv_label_create(screen);
    lv_obj_set_style_text_color(completed_label, lv_color_hex(0xFF6B6B), 0);
    lv_label_set_text(completed_label, "🍅x0");
    lv_obj_set_style_text_font(completed_label, &custom_font_16, 0);
    lv_obj_align(completed_label, LV_ALIGN_TOP_RIGHT, -10, 8);

    progress_arc = lv_arc_create(screen);
    lv_obj_set_size(progress_arc, 160, 160);
    lv_arc_set_rotation(progress_arc, 270);
    lv_arc_set_bg_angles(progress_arc, 0, 360);
    lv_arc_set_range(progress_arc, 0, 360);
    lv_arc_set_value(progress_arc, 360);
    lv_obj_set_style_arc_color(progress_arc, UI_COLOR_ARC_BG, LV_PART_MAIN);
    lv_obj_set_style_arc_color(progress_arc, lv_color_hex(0xFF6B6B), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(progress_arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(progress_arc, 10, LV_PART_INDICATOR);
    lv_obj_remove_style(progress_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(progress_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(progress_arc, LV_ALIGN_CENTER, 0, 0);

    timer_label = lv_label_create(screen);
    lv_obj_set_style_text_color(timer_label, UI_COLOR_TEXT, 0);
    lv_label_set_text(timer_label, "25:00");
    lv_obj_set_style_text_font(timer_label, &lv_font_montserrat_40, 0);
    lv_obj_align(timer_label, LV_ALIGN_CENTER, 0, 0);

    
    hint_label = ui_create_hint_label(screen, i18n(STR_H_SET_START_PAUSE_PRESS_STOP));


    update_pomo_display();
    timer_load_total();

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = pomo_on_encoder_cw,
        .on_encoder_ccw = pomo_on_encoder_ccw,
        .on_encoder_press = pomo_on_encoder_press,
        .on_settings_press = pomo_on_settings_press,
        .on_settings_long_press = pomo_on_settings_long_press,
        .on_long_press_hint = pomo_on_long_press_hint,
    };
    ui_register_input_callbacks(UI_SCREEN_POMODORO, &cbs);

    ESP_LOGI(TAG, "Pomodoro screen created");
    return screen;
}

/* ---- Periodic updates (called from UIUpdate task) ---- */

void ui_screen_pomodoro_update_state(uint8_t phase, uint32_t remaining_seconds, uint32_t completed, uint16_t current_cycle)
{
    if (s_timer_mode) return; /* timer mode handles its own display */

    pomodoro_settings_t settings = pomodoro_engine_get_settings();
    uint32_t color;
    const char *phase_text;
    switch (phase) {
        case 1:
            color = 0xFF6B6B;
            phase_text = i18n(STR_PHASE_WORK);
            total_seconds = settings.work_minutes * 60;
            break;
        case 2:
            color = 0x4CAF50;
            phase_text = i18n(STR_PHASE_BREAK);
            total_seconds = settings.break_minutes * 60;
            break;
        case 3:
            color = 0x4D96FF;
            phase_text = i18n(STR_PHASE_LONG_BREAK);
            total_seconds = settings.long_break_minutes * 60;
            break;
        case 4:
            color = 0xFFFF00;
            phase_text = i18n(STR_PHASE_PAUSED);
            total_seconds = remaining_seconds;
            break;
        default:
            color = 0xAAAAAA;
            phase_text = i18n(STR_PHASE_IDLE);
            total_seconds = settings.work_minutes * 60;
            break;
    }

    update_time_display(remaining_seconds);

    if (completed_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "🍅x%u", (unsigned int)completed);
        lv_label_set_text(completed_label, buf);
    }

    if (phase_label == NULL) return;

    lv_obj_set_style_text_color(phase_label, lv_color_hex(color), 0);
    lv_label_set_text(phase_label, phase_text);

    if (cycle_label) {
        char cycle_buf[20];
        snprintf(cycle_buf, sizeof(cycle_buf), "%u/%u", (unsigned int)current_cycle, settings.cycles_until_long_break);
        lv_label_set_text(cycle_label, cycle_buf);
    }

    if (progress_arc) {
        lv_obj_set_style_arc_color(progress_arc, lv_color_hex(color), LV_PART_INDICATOR);
    }
}

/* Called every second from UIUpdate task */
void ui_screen_pomodoro_timer_tick(void)
{
    if (!s_timer_mode) return;
    if (s_timer_state != TIMER_RUNNING) return;

    if (s_timer_remaining > 0) {
        s_timer_remaining--;
        update_timer_display();

        if (s_timer_remaining == 0) {
            /* Alarm! */
            s_timer_state = TIMER_ALARM;
            sound_service_play(SOUND_POMO_WORK_DONE);
            led_service_wait((led_color_t){255, 50, 50}, LED_WAIT_POMODORO);
            update_timer_display();
            ESP_LOGI(TAG, "Timer alarm!");
        }
    }
}

void ui_screen_pomodoro_refresh(void)
{
    timer_load_total();
    if (s_timer_mode) {
        update_timer_display();
    } else {
        update_pomo_display();
        if (hint_label) {
            lv_label_set_text(hint_label, i18n(STR_H_SET_START_PAUSE_PRESS_STOP));
        }
        if (phase_label) {
            pomodoro_state_t state = pomodoro_engine_get_state();
            const char *phase_text;
            switch (state.phase) {
                case POMODORO_PHASE_WORK:        phase_text = i18n(STR_PHASE_WORK); break;
                case POMODORO_PHASE_BREAK:       phase_text = i18n(STR_PHASE_BREAK); break;
                case POMODORO_PHASE_LONG_BREAK:  phase_text = i18n(STR_PHASE_LONG_BREAK); break;
                case POMODORO_PHASE_PAUSED:      phase_text = i18n(STR_PHASE_PAUSED); break;
                default:                         phase_text = i18n(STR_PHASE_IDLE); break;
            }
            lv_label_set_text(phase_label, phase_text);
        }
    }
}
