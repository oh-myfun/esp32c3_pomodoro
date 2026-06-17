#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <sys/param.h>
#include <time.h>
#include <unistd.h>

#include "lvgl.h"
#include "driver/st7789_lcd.h"
#include "driver/buzzer.h"
#include "driver/backlight.h"
#include "driver/aht20.h"
#include "driver/bmp280.h"
#include "driver/ws2812.h"
#include "input/input_handler.h"
#include "ui/ui_manager.h"
#include "ui/ui_screen_main.h"
#include "ui/ui_screen_pomodoro.h"
#include "ui/ui_screen_buddy.h"
#include "ui/ui_screen_wifi.h"
#include "ui/ui_screen_wifi_saved.h"
#include "service/wifi_service.h"
#include "service/time_service.h"
#include "service/storage_service.h"
#include "service/tcp_service.h"
#include "pomodoro/pomodoro_engine.h"
#include "buddy/buddy.h"
#include "service/sound_service.h"
#include "service/chime_service.h"
#include "service/led_service.h"
#include "ui/ui_screen_settings_debug.h"
#include "ui/ui_screen_settings_sound_demo.h"
#include "ui/ui_screen_settings_buddy.h"
#include "ui/ui_screen_bridge_scan.h"
#include "ui/ui_screen_sensor.h"
#include "service/sensor_service.h"
#include "ui/i18n.h"

static const char *TAG = "MAIN";

#define LVGL_DRAW_BUF_LINES 20
#define LVGL_TICK_PERIOD_MS 1
#define LVGL_TASK_MAX_DELAY_MS 100
#define LVGL_TASK_MIN_DELAY_MS 1
#define LVGL_TASK_STACK_SIZE (8 * 1024)
#define LVGL_TASK_PRIORITY 5

static lv_display_t *display = NULL;

/* ---- Idle / Sleep ---- */
#define SLEEP_OPTIONS_COUNT  7
#define DEEP_SLEEP_OPTIONS_COUNT  6
/* negative = seconds, positive = minutes */
static const int sleep_minutes[SLEEP_OPTIONS_COUNT] = {0, -10, -30, 1, 2, 5, 10};
static const int deep_sleep_minutes[DEEP_SLEEP_OPTIONS_COUNT] = {0, -10, 1, 2, 5, 10};
int sleep_timeout_idx = 3;   /* default: 1 min, accessed by settings UI */
int deep_sleep_timeout_idx = 0;  /* default: off */
bool keep_awake_on_busy = false;  /* buddy BUSY 时阻止休眠，由伙伴设置子屏更新 */

typedef enum {
    STAGE_AWAKE = 0,
    STAGE_LIGHT,    /* normal sleep: backlight=1, PM lock released */
    STAGE_DEEP,     /* light sleep via esp_light_sleep_start */
} sleep_stage_t;

static sleep_stage_t s_sleep_stage = STAGE_AWAKE;
static int64_t s_last_activity_us = 0;
static int64_t s_light_sleep_enter_us = 0;  /* when LIGHT_SLEEP stage began */
static uint8_t s_saved_brightness = 10;
static esp_pm_lock_handle_t s_pm_lock = NULL;

/* GPIO wake sources (must match input_handler.c pinout) */
#define WAKE_ENC_A    GPIO_NUM_4
#define WAKE_ENC_B    GPIO_NUM_5
#define WAKE_SET_KEY  GPIO_NUM_9
#define WAKE_ENC_KEY  GPIO_NUM_21

bool activity_touch(void)
{
    s_last_activity_us = esp_timer_get_time();
    if (s_sleep_stage != STAGE_AWAKE) {
        sleep_stage_t prev = s_sleep_stage;
        s_sleep_stage = STAGE_AWAKE;
        backlight_set_brightness(s_saved_brightness);
        if (s_pm_lock) esp_pm_lock_acquire(s_pm_lock);
        ESP_LOGI(TAG, "Wake up (from stage %d)", (int)prev);
        return true;
    }
    return false;
}

int get_sleep_timeout_minutes(void)
{
    return sleep_minutes[sleep_timeout_idx];
}

/* Compute adaptive timer wakeup so we never miss a pomodoro or chime event.
 * Returns microseconds until next wake; floored at 1s, capped at 5min.
 * Wake 1s before each event so the LIGHT_SLEEP tick has time to fire sound/UI. */
static uint64_t compute_next_wakeup_us(void)
{
    uint64_t wake_us = 5ULL * 60 * 1000000;  /* hard cap */

    pomodoro_state_t pomo = pomodoro_engine_get_state();
    if (pomo.phase != POMODORO_PHASE_IDLE && pomo.phase != POMODORO_PHASE_PAUSED) {
        uint64_t pomo_us = (uint64_t)pomo.remaining_seconds * 1000000ULL;
        if (pomo_us > 1000000) pomo_us -= 1000000;  /* wake 1s early */
        if (pomo_us < wake_us) wake_us = pomo_us;
    }

    time_t now = time(NULL);
    if (now > 0) {
        struct tm t;
        localtime_r(&now, &t);
        int min = t.tm_min;
        int minutes_to_next = (min < 30) ? (30 - min) : (60 - min);
        int64_t secs_to_next = (int64_t)minutes_to_next * 60 - t.tm_sec;
        int64_t chime_us = secs_to_next * 1000000LL - 1000000;  /* 1s early */
        if (chime_us > 0 && (uint64_t)chime_us < wake_us) wake_us = (uint64_t)chime_us;
    }

    if (wake_us < 1000000) wake_us = 1000000;
    return wake_us;
}

static void enter_deep_sleep(void)
{
    if (keep_awake_on_busy && buddy_get_info().state == BUDDY_BUSY) {
        ESP_LOGI(TAG, "Skip deep sleep: buddy busy");
        return;
    }

    ESP_LOGI(TAG, "Enter deep sleep");
    pomodoro_engine_on_deep_sleep_enter();

    ws2812_off();
    st7789_lcd_sleep();

    /* GPIO wake sources: keys active-low (level), encoder edges */
    gpio_wakeup_enable(WAKE_SET_KEY, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable(WAKE_ENC_KEY, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable(WAKE_ENC_A,   GPIO_INTR_ANYEDGE);
    gpio_wakeup_enable(WAKE_ENC_B,   GPIO_INTR_ANYEDGE);
    esp_sleep_enable_gpio_wakeup();

    uint64_t wake_us = compute_next_wakeup_us();
    esp_sleep_enable_timer_wakeup(wake_us);

    s_sleep_stage = STAGE_DEEP;
    int64_t sleep_start = esp_timer_get_time();
    ESP_LOGI(TAG, "Light sleep for %llu us", (unsigned long long)wake_us);

    esp_err_t err = esp_light_sleep_start();
    int64_t slept_us = esp_timer_get_time() - sleep_start;

    gpio_wakeup_disable(WAKE_SET_KEY);
    gpio_wakeup_disable(WAKE_ENC_KEY);
    gpio_wakeup_disable(WAKE_ENC_A);
    gpio_wakeup_disable(WAKE_ENC_B);

    st7789_lcd_wakeup();
    pomodoro_engine_on_deep_sleep_exit();

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    ESP_LOGI(TAG, "Woke after %llu us, cause=%d, err=%s",
             (unsigned long long)slept_us, (int)cause, esp_err_to_name(err));

    if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        /* Planned wake: drop into LIGHT_SLEEP, let normal tick handle events */
        s_sleep_stage = STAGE_LIGHT;
        s_light_sleep_enter_us = esp_timer_get_time();
    } else {
        /* GPIO wake: user interaction, fully restore */
        s_sleep_stage = STAGE_AWAKE;
        s_last_activity_us = esp_timer_get_time();
        backlight_set_brightness(s_saved_brightness);
        if (s_pm_lock) esp_pm_lock_acquire(s_pm_lock);
    }
}
static esp_timer_handle_t lvgl_tick_timer = NULL;

static lv_color_t *buf1 = NULL;
static lv_color_t *buf2 = NULL;

static void increase_lvgl_tick(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

void lvgl_init(void)
{
    ESP_LOGI(TAG, "Initializing LVGL");
    lv_init();

    ESP_LOGI(TAG, "Allocating draw buffers");
    buf1 = heap_caps_malloc(240 * LVGL_DRAW_BUF_LINES * sizeof(lv_color_t), MALLOC_CAP_DMA);
    buf2 = heap_caps_malloc(240 * LVGL_DRAW_BUF_LINES * sizeof(lv_color_t), MALLOC_CAP_DMA);

    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "Failed to allocate draw buffers");
        return;
    }

    ESP_LOGI(TAG, "Creating LVGL display");
    display = lv_display_create(240, 240);
    lv_display_set_flush_cb(display, st7789_lcd_flush);
    lv_display_set_buffers(display, buf1, buf2, 240 * LVGL_DRAW_BUF_LINES * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_PARTIAL);

    ESP_LOGI(TAG, "Creating LVGL tick timer");
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer);
    esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000);

    ESP_LOGI(TAG, "LVGL initialized");
}

static void lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task started");
    uint32_t time_till_next_ms = 0;
    while (1) {
        lvgl_lock();
        time_till_next_ms = lv_timer_handler();
        lvgl_unlock();
        time_till_next_ms = MAX(time_till_next_ms, LVGL_TASK_MIN_DELAY_MS);
        time_till_next_ms = MIN(time_till_next_ms, LVGL_TASK_MAX_DELAY_MS);
        usleep(1000 * time_till_next_ms);
    }
}

/* ---- Callback wiring ---- */

static void on_wifi_connected(const char *ip) {
    ESP_LOGI(TAG, "WiFi connected, IP: %s", ip ? ip : "null");
    sound_service_play(SOUND_WIFI_CONNECTED);
    time_service_request_sync();

    /* Auto-connect TCP bridge if host and session are both configured */
    char host[48];
    int port;
    char session[9] = {0};
    if (tcp_service_load_config(host, sizeof(host), &port) && host[0] &&
        tcp_service_load_pairing_code(session, sizeof(session)) && session[0]) {
        tcp_service_connect(host, port);
    }

    if (ui_get_current_screen() == UI_SCREEN_WIFI_SAVED) {
        lvgl_lock();
        ui_screen_wifi_saved_refresh();
        lvgl_unlock();
    }
}

static void on_wifi_disconnected(void) {
    ESP_LOGI(TAG, "WiFi disconnected");
    if (ui_get_current_screen() == UI_SCREEN_WIFI_SAVED) {
        lvgl_lock();
        ui_screen_wifi_saved_refresh();
        lvgl_unlock();
    }
}

static void on_wifi_scan_complete(int count) {
    ESP_LOGI(TAG, "WiFi scan complete, %d APs found", count);
}

// TCP callbacks
static void on_tcp_connected(void) {
    ESP_LOGI(TAG, "TCP bridge connected");
    buddy_on_tcp_connected();
    lvgl_lock();
    ui_screen_buddy_set_connected(true);
    lvgl_unlock();
}

static void on_tcp_disconnected(void) {
    ESP_LOGI(TAG, "TCP bridge disconnected");
    buddy_on_tcp_disconnected();
    lvgl_lock();
    ui_screen_buddy_set_connected(false);
    lvgl_unlock();
}

static void on_tcp_request(const tcp_request_t *req) {
    ESP_LOGI(TAG, "TCP request: tool=%s type=%d", req->tool, req->type);
    activity_touch();
    buddy_on_tcp_request(req);

    /* Build option labels + descriptions for the UI */
    const char *opt_labels[8] = {NULL};
    const char *opt_descs[8]  = {NULL};
    for (int i = 0; i < req->option_count && i < 8; i++) {
        opt_labels[i] = req->options[i].label[0] ? req->options[i].label : NULL;
        opt_descs[i]  = req->options[i].description[0] ? req->options[i].description : NULL;
    }

    lvgl_lock();
    ui_screen_buddy_show_request(req->tool, req->command, req->description, req->hint,
                                  req->option_count, req->type,
                                  opt_labels, req->option_count,
                                  opt_descs,
                                  req->permission_suggestions_json[0] != '\0',
                                  req->permission_suggestions_text[0] ? req->permission_suggestions_text
                                                                      : req->permission_suggestions_json);
    lvgl_unlock();
}

static void on_tcp_session_end(void) {
    ESP_LOGI(TAG, "TCP session ended");
    buddy_on_tcp_session_end();
}

static void on_tcp_request_done(const char *request_id) {
    ESP_LOGI(TAG, "TCP request done: %s", request_id ? request_id : "(none)");
    buddy_on_request_done(request_id);
}

static void on_tcp_status(const char *state, const char *message) {
    ESP_LOGI(TAG, "TCP status: %s", state);
    activity_touch();
    buddy_on_status(state, message);
}

static void on_tcp_paired(void) {
    ESP_LOGI(TAG, "TCP paired, updating UI");
    activity_touch();
    lvgl_lock();
    ui_screen_buddy_set_connected(true);
    lvgl_unlock();
}

// Buddy -> WS2812 + UI + TCP decision
static bool attn_forced_nav = false;

static void on_buddy_state_changed(buddy_state_t new_state) {
    ESP_LOGI(TAG, "Buddy state changed to %d", new_state);
    if (new_state == BUDDY_ATTENTION) {
        /* Force push: even top-level→top-level gets stacked so ui_go_back works */
        if (ui_get_current_screen() != UI_SCREEN_BUDDY) {
            ui_push_screen(UI_SCREEN_BUDDY);
            attn_forced_nav = true;
        } else {
            attn_forced_nav = false;
        }
    } else if (new_state == BUDDY_CELEBRATE || new_state == BUDDY_DIZZY || new_state == BUDDY_HEART) {
        /* Temporary animation states — don't navigate away, just clear ATTENTION overlay */
        lvgl_lock();
        ui_screen_buddy_clear_request();
        lvgl_unlock();
    } else if (attn_forced_nav) {
        /* ATTENTION forced nav ended — clear overlay and pop back */
        attn_forced_nav = false;
        lvgl_lock();
        ui_screen_buddy_clear_request();
        ui_go_back();
        lvgl_unlock();
    } else {
        /* Normal state change (idle/busy/sleep) — clear any leftover overlay */
        lvgl_lock();
        ui_screen_buddy_clear_request();
        lvgl_unlock();
    }
}

static void on_buddy_decision(bool approved, const tcp_request_t *req) {
    if (!req || !req->ccbb_request_id[0]) return;

    static char json[1536];
    static char answers[512];
    if (approved) {
        if (req->type == REQ_PERMISSION) {
            if (buddy_should_include_rules() && req->permission_suggestions_json[0]) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
                snprintf(json, sizeof(json),
                    "{\"ccbb_request_id\":\"%s\",\"behavior\":\"allow\","
                    "\"updatedPermissions\":%s}",
                    req->ccbb_request_id,
                    req->permission_suggestions_json);
#pragma GCC diagnostic pop
            } else {
                snprintf(json, sizeof(json),
                    "{\"ccbb_request_id\":\"%s\",\"behavior\":\"allow\"}",
                    req->ccbb_request_id);
            }
        } else {
            /* AskUserQuestion: build answers dict */
            snprintf(answers, sizeof(answers), "{}");
            int ac = buddy_get_answer_count();
            if (ac > 0 && req->question[0]) {
                int off = 0;
                off += snprintf(answers + off, sizeof(answers) - off,
                    "{\"%s\":", req->question);
                if (buddy_is_answer_multi()) {
                    off += snprintf(answers + off, sizeof(answers) - off, "[");
                    for (int i = 0; i < ac && i < 8; i++) {
                        if (i > 0) off += snprintf(answers + off, sizeof(answers) - off, ",");
                        const char *lbl = buddy_get_answer_label(i);
                        off += snprintf(answers + off, sizeof(answers) - off,
                            "\"%s\"", lbl ? lbl : "");
                    }
                    off += snprintf(answers + off, sizeof(answers) - off, "]");
                } else {
                    const char *lbl = buddy_get_answer_label(0);
                    off += snprintf(answers + off, sizeof(answers) - off,
                        "\"%s\"", lbl ? lbl : "");
                }
                off += snprintf(answers + off, sizeof(answers) - off, "}");
            }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
            snprintf(json, sizeof(json),
                "{\"ccbb_request_id\":\"%s\",\"behavior\":\"allow\","
                "\"updatedInput\":{\"questions\":%s,\"answers\":%s}}",
                req->ccbb_request_id,
                req->questions_json[0] ? req->questions_json : "[]",
                answers);
#pragma GCC diagnostic pop
        }
    } else {
        snprintf(json, sizeof(json),
            "{\"ccbb_request_id\":\"%s\",\"behavior\":\"deny\",\"message\":\"Denied by device\"}",
            req->ccbb_request_id);
    }
    ESP_LOGI(TAG, "Sending decision: %s", approved ? "allow" : "deny");
    tcp_service_send_decision(json);
}

/* ---- Tasks ---- */

static void service_task(void *arg) {
    ESP_LOGI(TAG, "Service task started");
    int64_t last_buddy_tick = 0;

    while (1) {
        int64_t now = esp_timer_get_time() / 1000;

        // Time service periodic sync
        time_service_tick();

        // TCP maintenance is handled by its own task

        // Buddy animation tick every 200ms (matches original TICK_MS)
        if (now - last_buddy_tick >= 200) {
            buddy_tick();
            last_buddy_tick = now;
        }

        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
}

static void ui_update_task(void *arg) {
    ESP_LOGI(TAG, "UI update task started");
    int64_t last_pomodoro_tick = 0;
    int64_t last_wifi_ui_tick = 0;
    int64_t last_mem_tick = 0;
    int64_t last_debug_tick = 0;
    /* Track synced state transitions so a NTP sync event triggers an immediate
     * UI refresh instead of waiting up to 1s for the next wifi_ui tick. */
    bool prev_main_synced = false;
    int64_t last_chime_tick = 0;

    while (1) {
        int64_t now = esp_timer_get_time() / 1000;
        ui_screen_id_t current_screen = ui_get_current_screen();

        // Sleep state machine
        if (s_sleep_stage == STAGE_AWAKE && sleep_minutes[sleep_timeout_idx] != 0) {
            /* buddy 工作时（Claude 正在执行任务）可选择阻止休眠 */
            bool buddy_busy = (buddy_get_info().state == BUDDY_BUSY);
            if (!(keep_awake_on_busy && buddy_busy)) {
                int64_t idle_ms = (esp_timer_get_time() - s_last_activity_us) / 1000;
                int val = sleep_minutes[sleep_timeout_idx];
                int64_t threshold_ms = (val < 0) ? (int64_t)(-val) * 1000LL : (int64_t)val * 60000LL;
                if (idle_ms >= threshold_ms) {
                    s_sleep_stage = STAGE_LIGHT;
                    s_light_sleep_enter_us = esp_timer_get_time();
                    s_saved_brightness = backlight_get_brightness();
                    backlight_set_brightness(1);
                    if (s_pm_lock) esp_pm_lock_release(s_pm_lock);
                    ESP_LOGI(TAG, "Light sleep (idle %llds)", idle_ms / 1000);
                }
            }
        } else if (s_sleep_stage == STAGE_LIGHT && deep_sleep_minutes[deep_sleep_timeout_idx] != 0) {
            /* After LIGHT_SLEEP timeout, escalate to DEEP_SLEEP (light sleep).
             * Skip if buddy is busy and keep_awake_on_busy is set. */
            bool buddy_busy = (buddy_get_info().state == BUDDY_BUSY);
            if (!(keep_awake_on_busy && buddy_busy)) {
                int64_t light_ms = (esp_timer_get_time() - s_light_sleep_enter_us) / 1000;
                int deep_val = deep_sleep_minutes[deep_sleep_timeout_idx];
                int64_t threshold_ms = (deep_val < 0) ? (int64_t)(-deep_val) * 1000LL : (int64_t)deep_val * 60000LL;
                if (light_ms >= threshold_ms) {
                    enter_deep_sleep();  /* returns after wake, stage set inside */
                }
            }
        }
        /* STAGE_DEEP: ui_update_task is frozen during esp_light_sleep_start;
         * this branch never runs while asleep. */

        // Pomodoro tick every 1 second
        if (now - last_pomodoro_tick >= 1000) {
            pomodoro_state_t prev = pomodoro_engine_get_state();
            pomodoro_engine_tick();
            pomodoro_state_t state = pomodoro_engine_get_state();

            if (state.phase != prev.phase) {
                if (prev.phase == POMODORO_PHASE_WORK) {
                    if (state.phase == POMODORO_PHASE_PAUSED) {
                        // Manual: work ended, waiting for user
                        sound_service_play(SOUND_POMO_WORK_DONE);
                        led_service_play(LED_COLOR_BREAK);
                        led_service_wait(LED_COLOR_BREAK, LED_WAIT_POMODORO);
                    } else if (state.phase == POMODORO_PHASE_BREAK) {
                        // Auto: directly starting break
                        sound_service_play(SOUND_POMO_BREAK_START);
                        led_service_play(LED_COLOR_BREAK);
                    } else if (state.phase == POMODORO_PHASE_LONG_BREAK) {
                        sound_service_play(SOUND_POMO_LONG_BREAK);
                        led_service_play(LED_COLOR_LONG_BREAK);
                    }
                } else if (prev.phase == POMODORO_PHASE_BREAK || prev.phase == POMODORO_PHASE_LONG_BREAK) {
                    if (state.phase == POMODORO_PHASE_PAUSED) {
                        // Manual: break ended, waiting for user
                        sound_service_play(SOUND_POMO_BREAK_DONE);
                        led_service_play(LED_COLOR_WORK);
                        led_service_wait(LED_COLOR_WORK, LED_WAIT_POMODORO);
                    } else if (state.phase == POMODORO_PHASE_WORK) {
                        // Auto: directly starting work
                        sound_service_play(SOUND_POMO_WORK_START);
                        led_service_play(LED_COLOR_WORK);
                    }
                }
            }

            lvgl_lock();
            ui_screen_pomodoro_update_state(state.phase, state.remaining_seconds, state.completed_count, state.current_cycle);
            ui_screen_pomodoro_timer_tick();
            lvgl_unlock();
            last_pomodoro_tick = now;
        }

        // Main screen: time update every tick
        if (current_screen == UI_SCREEN_MAIN) {
            lvgl_lock();
            ui_screen_main_update_time();
            lvgl_unlock();
        }

        // WiFi status: query real state from services, only push UI on main screen.
        // NTP sync completion flips synced=false→true; detect the transition and
        // refresh immediately instead of waiting up to 1s for the next tick.
        bool cur_main_synced = time_service_is_synced();
        bool sync_changed_on_main = (current_screen == UI_SCREEN_MAIN &&
                                     cur_main_synced != prev_main_synced);
        prev_main_synced = cur_main_synced;

        if (sync_changed_on_main || now - last_wifi_ui_tick >= 1000) {
            if (current_screen == UI_SCREEN_MAIN) {
                wifi_state_t wifi_state = wifi_service_get_state();
                lvgl_lock();
                if (wifi_state == WIFI_STATE_CONNECTED) {
                    bool synced = time_service_is_synced();
                    ui_screen_main_update_wifi_status(
                        synced ? i18n(STR_WIFI_CONNECTED) : i18n(STR_WIFI_SYNCING),
                        synced ? 0x00FF00 : 0xFFFF00);
                } else if (wifi_state == WIFI_STATE_SCANNING) {
                    ui_screen_main_update_wifi_status(i18n(STR_SCANNING_MAIN), 0xAAAAAA);
                } else if (wifi_state == WIFI_STATE_CONNECTING) {
                    ui_screen_main_update_wifi_status(i18n(STR_CONNECTING), 0xFFAA00);
                } else {
                    ui_screen_main_update_wifi_status(i18n(STR_NO_WIFI), 0x666666);
                }
                lvgl_unlock();
            }

            if (current_screen == UI_SCREEN_WIFI_LIST) {
                ui_screen_wifi_list_refresh();
            }
            last_wifi_ui_tick = now;
        }

        // Buddy screen state update
        if (current_screen == UI_SCREEN_BUDDY) {
            lvgl_lock();
            ui_screen_buddy_update_state();
            lvgl_unlock();
        }

        // Debug screen refresh every 1 second
        if (current_screen == UI_SCREEN_SETTINGS_DEBUG && now - last_debug_tick >= 1000) {
            lvgl_lock();
            ui_screen_settings_debug_refresh();
            lvgl_unlock();
            last_debug_tick = now;
        }

        // Sound demo: clear ▶ marker when buzzer finishes
        if (current_screen == UI_SCREEN_SETTINGS_SOUND_DEMO) {
            lvgl_lock();
            ui_screen_settings_sound_demo_update_play_state();
            lvgl_unlock();
        }

        // Buddy settings: refresh connect state every 1 second
        static int64_t last_buddy_set_tick = 0;
        if (current_screen == UI_SCREEN_SETTINGS_BUDDY && now - last_buddy_set_tick >= 1000) {
            lvgl_lock();
            ui_screen_settings_buddy_refresh();
            lvgl_unlock();
            last_buddy_set_tick = now;
        }

        // Bridge scan: refresh results every 500ms
        static int64_t last_bridge_scan_tick = 0;
        if (current_screen == UI_SCREEN_BRIDGE_SCAN && now - last_bridge_scan_tick >= 500) {
            lvgl_lock();
            ui_screen_bridge_scan_refresh();
            lvgl_unlock();
            last_bridge_scan_tick = now;
        }

        // Sensor page: refresh every 1 second
        static int64_t last_chart_tick = 0;
        if (current_screen == UI_SCREEN_SENSOR && now - last_chart_tick >= 1000) {
            lvgl_lock();
            ui_screen_sensor_update();
            lvgl_unlock();
            last_chart_tick = now;
        }

        // Chime service: check hour/half-hour boundary every 1 second
        if (now - last_chime_tick >= 1000) {
            chime_service_tick();
            last_chime_tick = now;
        }

        // Memory monitor every 30 seconds
        if (now - last_mem_tick >= 30000) {
            multi_heap_info_t info;
            heap_caps_get_info(&info, MALLOC_CAP_8BIT);
            ESP_LOGI(TAG, "[MEM] heap_free=%u  heap_min=%u",
                     (unsigned)info.total_free_bytes,
                     (unsigned)info.minimum_free_bytes);
            last_mem_tick = now;
        }

        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

/* ---- Entry point ---- */

void app_main(void) {
    ESP_LOGI(TAG, "Pomodoro Buddy Device Starting...");

    // 1. Fatal: NVS
    esp_err_t ret = nvs_flash_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        ESP_ERROR_CHECK(ret);  // halt
    }

    // 1.5. Migrate old NVS keys
    storage_migrate_settings_keys();

    // 2. Non-fatal: drivers
    buzzer_init();
    backlight_init();
    {
        int32_t bl_level = 10;
        storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_BL_BRIGHT, &bl_level);
        backlight_set_brightness((uint8_t)bl_level);
        ESP_LOGI(TAG, "LCD backlight level=%d", bl_level);
    }

    // 2.5. Power management: DFS auto-lowers CPU when idle
    {
        esp_pm_config_t pm_config = {
            .max_freq_mhz = 160,
            .min_freq_mhz = 40,
        };
        esp_pm_configure(&pm_config);
        esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "activity", &s_pm_lock);
        esp_pm_lock_acquire(s_pm_lock);
        ESP_LOGI(TAG, "PM: DFS enabled 160/40 MHz");
    }

    // 2.6. Load sleep timeout setting
    {
        int32_t val;
        if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_SLEEP_TIMEOUT, &val) && val >= 0 && val < SLEEP_OPTIONS_COUNT) {
            sleep_timeout_idx = (int)val;
        }
        ESP_LOGI(TAG, "Sleep timeout: %d", sleep_minutes[sleep_timeout_idx]);
    }
    // 2.6.1 Load deep-sleep timeout setting
    {
        int32_t val = 0;
        if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_DEEP_SLEEP_TIMEOUT, &val) && val >= 0 && val < DEEP_SLEEP_OPTIONS_COUNT) {
            deep_sleep_timeout_idx = (int)val;
        }
        ESP_LOGI(TAG, "Deep sleep timeout: %d min", deep_sleep_minutes[deep_sleep_timeout_idx]);
    }
    // 2.7. Load keep-awake-on-buddy-busy setting
    {
        int32_t val = 0;
        if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_KEEP_AWAKE_BUSY, &val)) {
            keep_awake_on_busy = (val != 0);
        }
        ESP_LOGI(TAG, "Keep awake on busy: %d", keep_awake_on_busy);
    }
    s_last_activity_us = esp_timer_get_time();

    st7789_lcd_init();
    aht20_init();
    bmp280_init();
    sensor_service_init();
    if (ws2812_init() != 0) ESP_LOGW(TAG, "WS2812 init failed, continuing");
    led_service_init();

    // 3. LVGL + display (depends on LCD)
    lvgl_init();

    // 4. Network services (before UI so timezone is available)
    if (wifi_service_init() != 0) ESP_LOGW(TAG, "WiFi service init failed, continuing");
    time_service_init();

    // 5. UI (depends on LVGL + time_service for timezone display)
    i18n_init();
    ui_init();

    // 6. Business modules (non-fatal)
    pomodoro_engine_init();
    if (buddy_init() != 0) ESP_LOGW(TAG, "Buddy init failed, continuing");

    // 7. Input (non-fatal)
    input_handler_init();

    // 7.5. TCP service (after buddy init)
    tcp_service_init();

    // 8. Register callbacks (wiring)
    static const wifi_callbacks_t wifi_cbs = {
        .on_connected = on_wifi_connected,
        .on_disconnected = on_wifi_disconnected,
        .on_scan_complete = on_wifi_scan_complete,
    };
    wifi_service_register_callbacks(&wifi_cbs);

    static const tcp_callbacks_t tcp_cbs = {
        .on_connected = on_tcp_connected,
        .on_disconnected = on_tcp_disconnected,
        .on_request = on_tcp_request,
        .on_session_end = on_tcp_session_end,
        .on_request_done = on_tcp_request_done,
        .on_status = on_tcp_status,
        .on_paired = on_tcp_paired,
    };
    tcp_service_register_callbacks(&tcp_cbs);

    static const buddy_callbacks_t buddy_cbs = {
        .on_state_changed = on_buddy_state_changed,
        .on_decision = on_buddy_decision,
    };
    buddy_register_callbacks(&buddy_cbs);

    sound_service_init();
    chime_service_init();

    // 9. Create tasks
    xTaskCreate(lvgl_port_task, "LVGL",    8192, NULL, 5, NULL);
    xTaskCreate(input_handler_task, "Input",   8192, NULL, 3, NULL);
    xTaskCreate(service_task, "Service", 4096, NULL, 2, NULL);
    xTaskCreate(ui_update_task, "UI",      4608, NULL, 1, NULL);

    ESP_LOGI(TAG, "All tasks created");

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
