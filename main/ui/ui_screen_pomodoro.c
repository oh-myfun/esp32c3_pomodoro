#include "ui_screen_pomodoro.h"
#include "i18n.h"
#include "custom_font.h"
#include "ui_manager.h"
#include "pomodoro/pomodoro_engine.h"
#include "service/sound_service.h"
#include "service/led_service.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_POMODORO";

static bool pomo_is_running(void)
{
    pomodoro_state_t state = pomodoro_engine_get_state();
    return state.phase != POMODORO_PHASE_IDLE && state.phase != POMODORO_PHASE_PAUSED;
}

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
    pomodoro_engine_stop();
    led_service_wait_done(LED_WAIT_POMODORO);
}

static void pomo_on_settings_press(void)
{
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

static lv_obj_t *progress_arc = NULL;
static lv_obj_t *timer_label = NULL;
static lv_obj_t *phase_label = NULL;
static lv_obj_t *completed_label = NULL;
static lv_obj_t *hint_label = NULL;
static lv_obj_t *cycle_label = NULL;

static uint32_t total_seconds = 25 * 60;

static void update_settings_display(void)
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
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)state.completed_count);
        lv_label_set_text(completed_label, buf);
    }
}

lv_obj_t* ui_screen_pomodoro_create(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_size(screen, 240, 240);

    cycle_label = lv_label_create(screen);
    lv_obj_set_style_text_color(cycle_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(cycle_label, "1/4");
    lv_obj_set_style_text_font(cycle_label, &custom_font_16, 0);
    lv_obj_align(cycle_label, LV_ALIGN_TOP_LEFT, 10, 8);

    phase_label = lv_label_create(screen);
    lv_obj_set_style_text_color(phase_label, lv_color_hex(0xFFAA00), 0);
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
    lv_obj_set_style_arc_color(progress_arc, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_arc_color(progress_arc, lv_color_hex(0xFF6B6B), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(progress_arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(progress_arc, 10, LV_PART_INDICATOR);
    lv_obj_remove_style(progress_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(progress_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(progress_arc, LV_ALIGN_CENTER, 0, 0);

    timer_label = lv_label_create(screen);
    lv_obj_set_style_text_color(timer_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(timer_label, "25:00");
    lv_obj_set_style_text_font(timer_label, &lv_font_montserrat_40, 0);
    lv_obj_align(timer_label, LV_ALIGN_CENTER, 0, 0);

    hint_label = lv_label_create(screen);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
    lv_label_set_text(hint_label, i18n(STR_H_SET_START_PAUSE_PRESS_STOP));
    lv_obj_set_style_text_font(hint_label, &custom_font_14, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    update_settings_display();

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = pomo_on_encoder_cw,
        .on_encoder_ccw = pomo_on_encoder_ccw,
        .on_encoder_press = pomo_on_encoder_press,
        .on_settings_press = pomo_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_POMODORO, &cbs);

    ESP_LOGI(TAG, "Pomodoro screen created");
    return screen;
}

static void update_time(uint32_t remaining_seconds)
{
    if (timer_label == NULL) return;
    
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

static void update_completed(uint32_t count)
{
    if (completed_label == NULL) return;
    char buf[16];
    snprintf(buf, sizeof(buf), "🍅x%u", (unsigned int)count);
    lv_label_set_text(completed_label, buf);
}

void ui_screen_pomodoro_update_state(uint8_t phase, uint32_t remaining_seconds, uint32_t completed, uint16_t current_cycle)
{
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
            total_seconds = remaining_seconds;  /* next phase time → full arc */
            break;
        default:
            color = 0xAAAAAA;
            phase_text = i18n(STR_PHASE_IDLE);
            total_seconds = settings.work_minutes * 60;
            break;
    }

    /* Update time display after total_seconds is set */
    update_time(remaining_seconds);
    update_completed(completed);

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

void ui_screen_pomodoro_refresh(void)
{
    update_settings_display();
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
