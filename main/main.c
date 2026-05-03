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
// #include "service/ble_service.h"  // BLE disabled
#include "pomodoro/pomodoro_engine.h"
#include "buddy/buddy.h"
#include "service/sound_service.h"

static const char *TAG = "MAIN";

#define LVGL_DRAW_BUF_LINES 30
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
static void on_wifi_connected(const char *ip) {
    ESP_LOGI(TAG, "WiFi connected, IP: %s", ip ? ip : "null");
    wifi_fail_time = 0;
    sound_service_play(SOUND_WIFI_CONNECTED);
    time_service_request_sync();
    // Status will be updated by ui_update_task polling
}

static void on_wifi_disconnected(void) {
    ESP_LOGI(TAG, "WiFi disconnected");
    // Status will be updated by ui_update_task polling
}

static void on_wifi_scan_complete(int count) {
    ESP_LOGI(TAG, "WiFi scan complete, %d APs found", count);
}

static void on_wifi_connect_failed(void) {
    ESP_LOGI(TAG, "WiFi connect failed");
    wifi_fail_time = esp_timer_get_time() / 1000;
    sound_service_play(SOUND_WIFI_FAILED);
}

// BLE callbacks disabled
// static void on_ble_connected(void) { ... }
// static void on_ble_disconnected(void) { ... }
// static void on_ble_heartbeat(const ble_heartbeat_t *hb) { ... }

// Buddy -> WS2812 + UI
static void on_buddy_state_changed(buddy_state_t new_state) {
    ESP_LOGI(TAG, "Buddy state changed to %d", new_state);
    if (new_state == BUDDY_ATTENTION) {
        lvgl_lock();
        ui_switch_screen(UI_SCREEN_BUDDY);
        lvgl_unlock();
    }
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

        // Buddy animation tick every 500ms
        if (now - last_buddy_tick >= 500) {
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
                    if (state.phase == POMODORO_PHASE_LONG_BREAK) {
                        sound_service_play(SOUND_POMO_LONG_BREAK);
                    } else if (state.phase == POMODORO_PHASE_BREAK) {
                        sound_service_play(SOUND_POMO_WORK_DONE);
                    }
                } else if (state.phase == POMODORO_PHASE_WORK &&
                           (prev.phase == POMODORO_PHASE_BREAK || prev.phase == POMODORO_PHASE_LONG_BREAK)) {
                    sound_service_play(SOUND_POMO_BREAK_DONE);
                }
            }

            lvgl_lock();
            ui_screen_pomodoro_update_state(state.phase, state.remaining_seconds, state.completed_count, state.current_cycle);
            lvgl_unlock();
            last_pomodoro_tick = now;
        }

        // Main screen time + WiFi status update
        if (current_screen == UI_SCREEN_MAIN) {
            lvgl_lock();
            ui_screen_main_update_time();

            // Update WiFi status based on current state
            wifi_state_t wifi_state = wifi_service_get_state();
            int64_t now_ms = esp_timer_get_time() / 1000;

            if (wifi_fail_time > 0 && (now_ms - wifi_fail_time) < 3000) {
                ui_screen_main_update_wifi_status("Connect Failed", 0xFF4444);
            } else {
                wifi_fail_time = 0;
                switch (wifi_state) {
                    case WIFI_STATE_DISCONNECTED:
                        ui_screen_main_update_wifi_status("No WiFi", 0x666666);
                        break;
                    case WIFI_STATE_SCANNING:
                        ui_screen_main_update_wifi_status("Scanning...", 0xAAAAAA);
                        break;
                    case WIFI_STATE_CONNECTING:
                        ui_screen_main_update_wifi_status("Connecting...", 0xFFAA00);
                        break;
                    case WIFI_STATE_CONNECTED: {
                        const char *ip = wifi_service_get_ip();
                        bool synced = time_service_is_synced();
                        char status[40];
                        if (synced) {
                            snprintf(status, sizeof(status), "IP:%s [OK]", ip ? ip : "");
                        } else {
                            snprintf(status, sizeof(status), "IP:%s [Sync..]", ip ? ip : "");
                        }
                        ui_screen_main_update_wifi_status(status, synced ? 0x00FF00 : 0xFFFF00);
                        break;
                    }
                }
            }
            lvgl_unlock();
        }

        // WiFi list & saved list refresh every 1 second
        if (now - last_wifi_ui_tick >= 1000) {
            if (current_screen == UI_SCREEN_WIFI_LIST) {
                ui_screen_wifi_list_refresh();
            }

            if (current_screen == UI_SCREEN_WIFI_SAVED) {
                lvgl_lock();
                ui_screen_wifi_saved_refresh();
                lvgl_unlock();
            }

            last_wifi_ui_tick = now;
        }

        // Buddy screen state update
        if (current_screen == UI_SCREEN_BUDDY) {
            lvgl_lock();
            ui_screen_buddy_update_state();
            lvgl_unlock();
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

    // 2. Non-fatal: drivers
    buzzer_init();
    st7789_lcd_init();
    if (ws2812_init() != 0) ESP_LOGW(TAG, "WS2812 init failed, continuing");

    // 3. LVGL + display (depends on LCD)
    lvgl_init();

    // 4. Network services (before UI so timezone is available)
    if (wifi_service_init() != 0) ESP_LOGW(TAG, "WiFi service init failed, continuing");
    time_service_init();

    // 5. UI (depends on LVGL + time_service for timezone display)
    ui_init();

    // 6. Business modules (non-fatal)
    pomodoro_engine_init();
    if (buddy_init() != 0) ESP_LOGW(TAG, "Buddy init failed, continuing");

    // 7. Input (non-fatal)
    input_handler_init();

    // 8. Register callbacks (wiring)
    static const wifi_callbacks_t wifi_cbs = {
        .on_connected = on_wifi_connected,
        .on_disconnected = on_wifi_disconnected,
        .on_scan_complete = on_wifi_scan_complete,
        .on_connect_failed = on_wifi_connect_failed,
    };
    wifi_service_register_callbacks(&wifi_cbs);

    // BLE callbacks disabled
    // static const ble_callbacks_t ble_cbs = {
    //     .on_connected = on_ble_connected,
    //     .on_disconnected = on_ble_disconnected,
    //     .on_heartbeat = on_ble_heartbeat,
    // };
    // ble_service_register_callbacks(&ble_cbs);

    static const buddy_callbacks_t buddy_cbs = {
        .on_state_changed = on_buddy_state_changed,
    };
    buddy_register_callbacks(&buddy_cbs);

    sound_service_init();

    // 9. Create tasks
    xTaskCreate(lvgl_port_task, "LVGL",    8192, NULL, 5, NULL);
    xTaskCreate(input_handler_task, "Input",   6144, NULL, 3, NULL);
    xTaskCreate(service_task, "Service", 6144, NULL, 2, NULL);
    xTaskCreate(ui_update_task, "UI",      4096, NULL, 1, NULL);

    ESP_LOGI(TAG, "All tasks created");

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
