#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <sys/param.h>
#include <unistd.h>

#include "lvgl.h"
#include "driver/st7789_lcd.h"
#include "driver/buzzer.h"
#include "driver/backlight.h"
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
#include "service/led_service.h"
#include "ui/ui_screen_settings_debug.h"
#include "ui/ui_screen_settings_buddy.h"
#include "ui/ui_screen_bridge_scan.h"
#include "ui/i18n.h"

static const char *TAG = "MAIN";

#define LVGL_DRAW_BUF_LINES 20
#define LVGL_TICK_PERIOD_MS 1
#define LVGL_TASK_MAX_DELAY_MS 10
#define LVGL_TASK_MIN_DELAY_MS 1
#define LVGL_TASK_STACK_SIZE (8 * 1024)
#define LVGL_TASK_PRIORITY 5

static lv_display_t *display = NULL;
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

void lvgl_deinit(void)
{
    if (lvgl_tick_timer) {
        esp_timer_stop(lvgl_tick_timer);
        esp_timer_delete(lvgl_tick_timer);
        lvgl_tick_timer = NULL;
    }

    if (display) {
        lv_display_delete(display);
        display = NULL;
    }

    if (buf1) {
        free(buf1);
        buf1 = NULL;
    }

    if (buf2) {
        free(buf2);
        buf2 = NULL;
    }
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

static int64_t wifi_fail_time = 0;  // Timestamp of last connect failure

// WiFi -> time_service + UI
static int64_t wifi_connected_since = 0;  // 0 = not confirmed connected

static void on_wifi_connected(const char *ip) {
    ESP_LOGI(TAG, "WiFi connected, IP: %s", ip ? ip : "null");
    wifi_fail_time = 0;
    wifi_connected_since = esp_timer_get_time() / 1000;
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
    wifi_connected_since = 0;
    if (ui_get_current_screen() == UI_SCREEN_WIFI_SAVED) {
        lvgl_lock();
        ui_screen_wifi_saved_refresh();
        lvgl_unlock();
    }
}

static void on_wifi_scan_complete(int count) {
    ESP_LOGI(TAG, "WiFi scan complete, %d APs found", count);
}

static void on_wifi_connect_failed(void) {
    ESP_LOGI(TAG, "WiFi connect failed");
    wifi_fail_time = esp_timer_get_time() / 1000;
    sound_service_play(SOUND_WIFI_FAILED);
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
                                  req->permission_suggestions_json[0] != '\0');
    lvgl_unlock();
}

static void on_tcp_session_end(void) {
    ESP_LOGI(TAG, "TCP session ended");
    buddy_on_tcp_session_end();
}

static void on_tcp_status(const char *state, const char *message) {
    ESP_LOGI(TAG, "TCP status: %s", state);
    buddy_on_status(state, message);
}

static void on_tcp_paired(void) {
    ESP_LOGI(TAG, "TCP paired, updating UI");
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
    } else if (attn_forced_nav) {
        /* ATTENTION forced nav — clear overlay and pop back */
        attn_forced_nav = false;
        lvgl_lock();
        ui_screen_buddy_clear_request();
        ui_go_back();
        lvgl_unlock();
    } else {
        /* Normal state change on buddy screen (random animation, manual approve/deny) */
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

        // BLE maintenance (disabled)
        // ble_service_tick();

        // TCP maintenance is handled by its own task

        // Buddy animation tick every 200ms (matches original TICK_MS)
        if (now - last_buddy_tick >= 200) {
            buddy_tick();
            last_buddy_tick = now;
        }

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

static void ui_update_task(void *arg) {
    ESP_LOGI(TAG, "UI update task started");
    int64_t last_pomodoro_tick = 0;
    int64_t last_wifi_ui_tick = 0;
    int64_t last_mem_tick = 0;
    int64_t last_debug_tick = 0;

    while (1) {
        int64_t now = esp_timer_get_time() / 1000;
        ui_screen_id_t current_screen = ui_get_current_screen();

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
                        led_service_wait(LED_COLOR_BREAK);
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
                        led_service_wait(LED_COLOR_WORK);
                    } else if (state.phase == POMODORO_PHASE_WORK) {
                        // Auto: directly starting work
                        sound_service_play(SOUND_POMO_WORK_START);
                        led_service_play(LED_COLOR_WORK);
                    }
                }
            }

            lvgl_lock();
            ui_screen_pomodoro_update_state(state.phase, state.remaining_seconds, state.completed_count, state.current_cycle);
            lvgl_unlock();
            last_pomodoro_tick = now;
        }

        // Main screen: time update every tick, WiFi status every 1 second
        if (current_screen == UI_SCREEN_MAIN) {
            lvgl_lock();
            ui_screen_main_update_time();

            if (now - last_wifi_ui_tick >= 1000) {
                wifi_state_t wifi_state = wifi_service_get_state();
                int64_t now_ms = esp_timer_get_time() / 1000;

                if (wifi_fail_time > 0 && (now_ms - wifi_fail_time) < 3000) {
                    ui_screen_main_update_wifi_status(i18n(STR_CONNECT_FAILED), 0xFF4444);
                } else if (wifi_state == WIFI_STATE_CONNECTED) {
                    wifi_fail_time = 0;
                    wifi_connected_since = now_ms;
                    bool synced = time_service_is_synced();
                    ui_screen_main_update_wifi_status(
                        synced ? i18n(STR_WIFI_CONNECTED) : i18n(STR_WIFI_SYNCING),
                        synced ? 0x00FF00 : 0xFFFF00);
                } else if (wifi_state == WIFI_STATE_SCANNING) {
                    ui_screen_main_update_wifi_status(i18n(STR_SCANNING_MAIN), 0xAAAAAA);
                } else if (wifi_state == WIFI_STATE_CONNECTING) {
                    ui_screen_main_update_wifi_status(i18n(STR_CONNECTING), 0xFFAA00);
                } else if (wifi_connected_since > 0 && (now_ms - wifi_connected_since) < 5000) {
                    // Recently connected, brief disconnection — keep showing connected
                } else {
                    wifi_connected_since = 0;
                    ui_screen_main_update_wifi_status(i18n(STR_NO_WIFI), 0x666666);
                }
            }
            lvgl_unlock();
        }

        // WiFi list & saved list refresh every 1 second
        if (now - last_wifi_ui_tick >= 1000) {
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

        // Memory monitor every 30 seconds
        if (now - last_mem_tick >= 30000) {
            multi_heap_info_t info;
            heap_caps_get_info(&info, MALLOC_CAP_8BIT);
            ESP_LOGI(TAG, "[MEM] heap_free=%u  heap_min=%u",
                     (unsigned)info.total_free_bytes,
                     (unsigned)info.minimum_free_bytes);
            last_mem_tick = now;
        }

        vTaskDelay(100 / portTICK_PERIOD_MS);
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
    // backlight_init();  // TODO: GPIO20 backlight PWM, waiting for HW rework
    buzzer_init();
    st7789_lcd_init();
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
        .on_connect_failed = on_wifi_connect_failed,
    };
    wifi_service_register_callbacks(&wifi_cbs);

    static const tcp_callbacks_t tcp_cbs = {
        .on_connected = on_tcp_connected,
        .on_disconnected = on_tcp_disconnected,
        .on_request = on_tcp_request,
        .on_session_end = on_tcp_session_end,
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

    // 9. Create tasks
    xTaskCreate(lvgl_port_task, "LVGL",    8192, NULL, 5, NULL);
    xTaskCreate(input_handler_task, "Input",   5120, NULL, 3, NULL);
    xTaskCreate(service_task, "Service", 4096, NULL, 2, NULL);
    xTaskCreate(ui_update_task, "UI",      4608, NULL, 1, NULL);

    ESP_LOGI(TAG, "All tasks created");

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
