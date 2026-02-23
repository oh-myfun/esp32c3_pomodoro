#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <sys/lock.h>
#include <sys/param.h>
#include <unistd.h>

#include "lvgl.h"
#include "driver/st7789_lcd.h"
#include "driver/buzzer.h"
#include "input/input_handler.h"
#include "ui/ui_manager.h"
#include "ui/ui_screen_wifi.h"
#include "wifi_manager.h"
#include "pomodoro_engine.h"
#include "time_service.h"

static const char *TAG = "MAIN";

#define LVGL_DRAW_BUF_LINES 60
#define LVGL_TICK_PERIOD_MS 1
#define LVGL_TASK_MAX_DELAY_MS 10
#define LVGL_TASK_MIN_DELAY_MS 1
#define LVGL_TASK_STACK_SIZE (8 * 1024)
#define LVGL_TASK_PRIORITY 5

static _lock_t lvgl_api_lock;
static lv_display_t *display = NULL;

static void increase_lvgl_tick(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

void lvgl_init(void)
{
    lv_init();

    display = lv_display_create(240, 240);

    size_t draw_buffer_sz = 240 * LVGL_DRAW_BUF_LINES * sizeof(lv_color16_t);

    void *buf1 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_DMA);
    assert(buf1);
    void *buf2 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_DMA);
    assert(buf2);

    lv_display_set_buffers(display, buf1, buf2, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, st7789_lcd_flush);

    const esp_timer_create_args_t lvgl_tick_timer_args = {.callback = &increase_lvgl_tick, .name = "lvgl_tick"};
    esp_timer_handle_t lvgl_tick_timer = NULL;
    esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer);
    esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000);
}

static void lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task started");
    uint32_t time_till_next_ms = 0;
    while (1) {
        _lock_acquire(&lvgl_api_lock);
        time_till_next_ms = lv_timer_handler();
        _lock_release(&lvgl_api_lock);
        time_till_next_ms = MAX(time_till_next_ms, LVGL_TASK_MIN_DELAY_MS);
        time_till_next_ms = MIN(time_till_next_ms, LVGL_TASK_MAX_DELAY_MS);
        usleep(1000 * time_till_next_ms);
    }
}

static void time_update_task(void *arg)
{
    ESP_LOGI(TAG, "Time update task started");

    while (1) {
        _lock_acquire(&lvgl_api_lock);
        ui_update_time();
        ui_update_temp(25.5f);
        ui_update_humidity(65.0f);
        _lock_release(&lvgl_api_lock);

        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

static void pomodoro_task(void *arg)
{
    ESP_LOGI(TAG, "Pomodoro task started");
    
    while (1) {
        pomodoro_engine_tick();
        
        pomodoro_state_t state = pomodoro_engine_get_state();
        
        _lock_acquire(&lvgl_api_lock);
        ui_pomodoro_update_state(state.phase, state.remaining_seconds, state.completed_count);
        _lock_release(&lvgl_api_lock);
        
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

static void wifi_status_task(void *arg)
{
    ESP_LOGI(TAG, "WiFi status task started");
    
    int64_t last_sync_time = 0;
    bool has_synced = false;

    while (1) {
        _lock_acquire(&lvgl_api_lock);

        if (ui_get_current_screen() == UI_SCREEN_WIFI_LIST) {
            ui_screen_wifi_list_refresh();
            
            if (!wifi_manager_is_scan_done()) {
                _lock_release(&lvgl_api_lock);
                vTaskDelay(100 / portTICK_PERIOD_MS);
                continue;
            }
        }

        if (wifi_manager_is_connected()) {
            int interval = wifi_manager_get_ntp_interval();
            int64_t now = esp_timer_get_time() / 1000000;
            
            if (!has_synced) {
                wifi_manager_sync_time();
                has_synced = true;
                last_sync_time = now;
            } else if (interval > 0 && (now - last_sync_time >= interval * 60)) {
                ESP_LOGI(TAG, "Periodic NTP sync triggered");
                wifi_manager_sync_time();
                last_sync_time = now;
            }
        } else {
            has_synced = false;
            last_sync_time = 0;
        }

        if (ui_get_current_screen() == UI_SCREEN_MAIN) {
            if (wifi_manager_is_connected()) {
                const char *ip = wifi_manager_get_ip_address();
                char status[32];
                sprintf(status, "IP: %s", ip ? ip : "");
                ui_update_wifi_status_ex(status, 0x00FF00);
            } else {
                wifi_mode_state_t mode = wifi_manager_get_state();
                if (mode == WIFI_STATE_CONNECTING) {
                    ui_update_wifi_status_ex("Connecting...", 0xFFFF00);
                } else if (mode == WIFI_STATE_SCANNING) {
                    ui_update_wifi_status_ex("Scanning...", 0xFFFF00);
                } else if (wifi_manager_is_connect_failed()) {
                    ui_update_wifi_status_ex("Connect Failed", 0xFF0000);
                } else {
                    ui_update_wifi_status_ex("Disconnected", 0x666666);
                }
            }
        }

        _lock_release(&lvgl_api_lock);

        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "================================");
    ESP_LOGI(TAG, "ST7789 + LVGL + EC11 WiFi Demo");
    ESP_LOGI(TAG, "================================");
    ESP_LOGI(TAG, "LVGL: tick=%dms, max_delay=%dms, priority=%d, buf_lines=%d",
             LVGL_TICK_PERIOD_MS, LVGL_TASK_MAX_DELAY_MS, LVGL_TASK_PRIORITY, LVGL_DRAW_BUF_LINES);

    ESP_ERROR_CHECK(nvs_flash_init());
    
    st7789_lcd_init();
    buzzer_init();
    lvgl_init();
    ui_init();
    pomodoro_engine_init();
    input_handler_init();
    wifi_manager_init();
    time_service_init();

    xTaskCreate(lvgl_port_task, "LVGL", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL);
    xTaskCreate(input_handler_task, "Input", 16384, NULL, 2, NULL);
    xTaskCreate(time_update_task, "TimeUpdate", 4096, NULL, 1, NULL);
    xTaskCreate(wifi_status_task, "WiFiStatus", 4096, NULL, 1, NULL);
    xTaskCreate(pomodoro_task, "Pomodoro", 4096, NULL, 1, NULL);

    ESP_LOGI(TAG, "All tasks created");
    
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
