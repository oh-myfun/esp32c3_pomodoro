#include "esp_err.h"
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
#include "service/wifi_service.h"
#include "service/time_service.h"
#include "service/storage_service.h"
#include "service/ble_service.h"
#include "pomodoro/pomodoro_engine.h"
#include "buddy/buddy.h"

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
        time_till_next_ms = lv_timer_handler();
        time_till_next_ms = MAX(time_till_next_ms, LVGL_TASK_MIN_DELAY_MS);
        time_till_next_ms = MIN(time_till_next_ms, LVGL_TASK_MAX_DELAY_MS);
        usleep(1000 * time_till_next_ms);
    }
}

/* ---- Callback wiring ---- */

// WiFi -> time_service + UI
static void on_wifi_connected(const char *ip) {
    ESP_LOGI(TAG, "WiFi connected, IP: %s", ip ? ip : "null");
    time_service_request_sync();
    lvgl_lock();
    char status[32];
    snprintf(status, sizeof(status), "IP: %s", ip ? ip : "");
    ui_screen_main_update_wifi_status(status, 0x00FF00);
    lvgl_unlock();
}

static void on_wifi_disconnected(void) {
    ESP_LOGI(TAG, "WiFi disconnected");
    lvgl_lock();
    ui_screen_main_update_wifi_status("Disconnected", 0x666666);
    lvgl_unlock();
}

static void on_wifi_scan_complete(int count) {
    ESP_LOGI(TAG, "WiFi scan complete, %d APs found", count);
}

static void on_wifi_connect_failed(void) {
    ESP_LOGI(TAG, "WiFi connect failed");
    lvgl_lock();
    ui_screen_main_update_wifi_status("Connect Failed", 0xFF0000);
    lvgl_unlock();
}

// BLE -> buddy
static void on_ble_connected(void) {
    ESP_LOGI(TAG, "BLE connected");
    buddy_on_ble_connected();
}

static void on_ble_disconnected(void) {
    ESP_LOGI(TAG, "BLE disconnected");
    buddy_on_ble_disconnected();
}

static void on_ble_heartbeat(const ble_heartbeat_t *hb) {
    ESP_LOGD(TAG, "BLE heartbeat: %d running, %d waiting, prompt=%d",
             hb->running, hb->waiting, hb->has_prompt);
    buddy_on_heartbeat(hb->running, hb->waiting, hb->has_prompt,
                       hb->prompt_id, hb->prompt_tool, hb->prompt_hint);
}

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

        // BLE maintenance
        ble_service_tick();

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

    while (1) {
        int64_t now = esp_timer_get_time() / 1000;
        ui_screen_id_t current_screen = ui_get_current_screen();

        // Pomodoro tick every 1 second
        if (now - last_pomodoro_tick >= 1000) {
            pomodoro_engine_tick();
            pomodoro_state_t state = pomodoro_engine_get_state();
            lvgl_lock();
            ui_screen_pomodoro_update_state(state.phase, state.remaining_seconds, state.completed_count);
            lvgl_unlock();
            last_pomodoro_tick = now;
        }

        // Main screen time update
        if (current_screen == UI_SCREEN_MAIN) {
            lvgl_lock();
            ui_screen_main_update_time();
            lvgl_unlock();
        }

        // WiFi list refresh
        if (current_screen == UI_SCREEN_WIFI_LIST) {
            ui_screen_wifi_list_refresh();
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

    // 3. LVGL + UI (depends on LCD)
    lvgl_init();
    ui_init();

    // 4. Business modules (non-fatal)
    pomodoro_engine_init();
    if (buddy_init() != 0) ESP_LOGW(TAG, "Buddy init failed, continuing");

    // 5. Input (non-fatal)
    input_handler_init();

    // 6. Network services (non-fatal, last)
    if (wifi_service_init() != 0) ESP_LOGW(TAG, "WiFi service init failed, continuing");
    if (ble_service_init() != 0) ESP_LOGW(TAG, "BLE service init failed, continuing");

    // 7. Time service (non-fatal)
    time_service_init();

    // 8. Register callbacks (wiring)
    static const wifi_callbacks_t wifi_cbs = {
        .on_connected = on_wifi_connected,
        .on_disconnected = on_wifi_disconnected,
        .on_scan_complete = on_wifi_scan_complete,
        .on_connect_failed = on_wifi_connect_failed,
    };
    wifi_service_register_callbacks(&wifi_cbs);

    static const ble_callbacks_t ble_cbs = {
        .on_connected = on_ble_connected,
        .on_disconnected = on_ble_disconnected,
        .on_heartbeat = on_ble_heartbeat,
    };
    ble_service_register_callbacks(&ble_cbs);

    static const buddy_callbacks_t buddy_cbs = {
        .on_state_changed = on_buddy_state_changed,
    };
    buddy_register_callbacks(&buddy_cbs);

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
