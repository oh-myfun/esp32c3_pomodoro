#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <sys/param.h>
#include <unistd.h>

#include "esp_lvgl_port.h"
#include "driver/st7789_lcd.h"
#include "driver/buzzer.h"
#include "input/input_handler.h"
#include "ui/ui_manager.h"
#include "ui/ui_screen_wifi.h"
#include "network/wifi_manager.h"
#include "pomodoro/pomodoro_engine.h"
#include "time/time_service.h"

static const char *TAG = "MAIN";

#define LVGL_DRAW_BUF_LINES 60
#define LVGL_TICK_PERIOD_MS 1
#define LVGL_TASK_MAX_DELAY_MS 10
#define LVGL_TASK_MIN_DELAY_MS 1
#define LVGL_TASK_STACK_SIZE (8 * 1024)
#define LVGL_TASK_PRIORITY 5

static lv_display_t *display = NULL;
static void *buf1 = NULL;
static void *buf2 = NULL;
static esp_timer_handle_t lvgl_tick_timer = NULL;

static void increase_lvgl_tick(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

void lvgl_deinit(void)
{
    if (lvgl_tick_timer) {
        esp_timer_stop(lvgl_tick_timer);
        esp_timer_delete(lvgl_tick_timer);
        lvgl_tick_timer = NULL;
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

void lvgl_init(void)
{
    lvgl_port_lock(0);
    lv_init();
    lvgl_port_unlock();

    display = lv_display_create(240, 240);

    size_t draw_buffer_sz = 240 * LVGL_DRAW_BUF_LINES * sizeof(lv_color16_t);

    buf1 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_DMA);
    assert(buf1);
    buf2 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_DMA);
    assert(buf2);

    lvgl_port_lock(0);
    lv_display_set_buffers(display, buf1, buf2, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, st7789_lcd_flush);
    lvgl_port_unlock();

    const esp_timer_create_args_t lvgl_tick_timer_args = {.callback = &increase_lvgl_tick, .name = "lvgl_tick"};
    esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer);
    esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000);
}

static void lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task started");
    uint32_t time_till_next_ms = 0;
    while (1) {
        lvgl_port_lock(0);
        time_till_next_ms = lv_timer_handler();
        lvgl_port_unlock();
        time_till_next_ms = MAX(time_till_next_ms, LVGL_TASK_MIN_DELAY_MS);
        time_till_next_ms = MIN(time_till_next_ms, LVGL_TASK_MAX_DELAY_MS);
        usleep(1000 * time_till_next_ms);
    }
}

static void ui_update_task(void *arg)
{
    ESP_LOGI(TAG, "UI update task started");
    
    int64_t last_pomodoro_tick = 0;
    int64_t last_wifi_sync_time = 0;
    bool has_synced = false;
    wifi_mode_state_t last_wifi_state = WIFI_STATE_NONE;
    bool last_wifi_connected = false;
    
    while (1) {
        int64_t now = esp_timer_get_time() / 1000;
        
        lvgl_port_lock(0);
        
        ui_screen_id_t current_screen = ui_get_current_screen();
        
        if (now - last_pomodoro_tick >= 1000) {
            pomodoro_engine_tick();
            pomodoro_state_t state = pomodoro_engine_get_state();
            ui_pomodoro_update_state(state.phase, state.remaining_seconds, state.completed_count);
            last_pomodoro_tick = now;
        }
        
        if (current_screen == UI_SCREEN_MAIN) {
            ui_update_time();
            ui_update_temp(25.5f);
            ui_update_humidity(65.0f);
            
            bool wifi_connected = wifi_manager_is_connected();
            if (wifi_connected != last_wifi_connected) {
                last_wifi_connected = wifi_connected;
                if (wifi_connected) {
                    const char *ip = wifi_manager_get_ip_address();
                    char status[32];
                    snprintf(status, sizeof(status), "IP: %s", ip ? ip : "");
                    ui_update_wifi_status_ex(status, 0x00FF00);
                }
            } else if (!wifi_connected) {
                wifi_mode_state_t mode = wifi_manager_get_state();
                if (mode != last_wifi_state) {
                    last_wifi_state = mode;
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
        }
        
        if (current_screen == UI_SCREEN_WIFI_LIST) {
            ui_screen_wifi_list_refresh();
        }
        
        lvgl_port_unlock();
        
        if (wifi_manager_is_connected()) {
            int interval = wifi_manager_get_ntp_interval();
            int64_t now_sec = esp_timer_get_time() / 1000000;
            
            if (!has_synced) {
                wifi_manager_sync_time();
                has_synced = true;
                last_wifi_sync_time = now_sec;
            } else if (interval > 0 && (now_sec - last_wifi_sync_time >= interval * 60)) {
                wifi_manager_sync_time();
                last_wifi_sync_time = now_sec;
            }
        } else {
            has_synced = false;
        }
        
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Pomodoro Device Starting...");

    ESP_ERROR_CHECK(nvs_flash_init());
    
    st7789_lcd_init();
    buzzer_init();
    lvgl_init();
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&lvgl_cfg);
    ui_init();
    pomodoro_engine_init();
    input_handler_init();
    wifi_manager_init();
    time_service_init();

    xTaskCreatePinnedToCore(lvgl_port_task, "LVGL", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL, 0);
    xTaskCreatePinnedToCore(input_handler_task, "Input", LVGL_TASK_STACK_SIZE, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(ui_update_task, "UIUpdate", 4096, NULL, 1, NULL, 0);

    ESP_LOGI(TAG, "All tasks created");
    
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
