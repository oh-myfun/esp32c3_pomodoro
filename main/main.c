#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <sys/lock.h>
#include <sys/param.h>
#include <unistd.h>

#include "lvgl.h"
#include "encoder.h"
#include "ui_manager.h"

static const char *TAG = "MAIN";

// ===================== LCD引脚配置 =====================
#define LCD_RS_GPIO GPIO_NUM_10  // DC/RS
#define LCD_SCK_GPIO GPIO_NUM_6  // SCK
#define LCD_SDA_GPIO GPIO_NUM_7  // SDA
// =====================================================

#define LCD_V_RES 240
#define LCD_H_RES 240
#define LVGL_DRAW_BUF_LINES 20
#define LVGL_TICK_PERIOD_MS 2
#define LVGL_TASK_MAX_DELAY_MS 500
#define LVGL_TASK_MIN_DELAY_MS 1000 / CONFIG_FREERTOS_HZ
#define LVGL_TASK_STACK_SIZE (8 * 1024)
#define LVGL_TASK_PRIORITY 2

static _lock_t lvgl_api_lock;
static lv_display_t *display = NULL;

// GPIO初始化
static void lcd_gpio_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LCD_RS_GPIO) | (1ULL << LCD_SCK_GPIO) | (1ULL << LCD_SDA_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io_conf);

    gpio_set_level(LCD_SCK_GPIO, 0);
    gpio_set_level(LCD_SDA_GPIO, 0);
    gpio_set_level(LCD_RS_GPIO, 0);
}

// 软件SPI写8位数据（1us延时）
static void lcd_spi_write_byte(uint8_t data)
{
    for (int i = 0; i < 8; i++) {
        gpio_set_level(LCD_SCK_GPIO, 0);
        esp_rom_delay_us(1);

        if (data & 0x80) {
            gpio_set_level(LCD_SDA_GPIO, 1);
        } else {
            gpio_set_level(LCD_SDA_GPIO, 0);
        }

        gpio_set_level(LCD_SCK_GPIO, 1);
        esp_rom_delay_us(1);

        data <<= 1;
    }
    gpio_set_level(LCD_SCK_GPIO, 0);
}

// 写指令
static void lcd_write_cmd(uint8_t cmd)
{
    gpio_set_level(LCD_RS_GPIO, 0);
    lcd_spi_write_byte(cmd);
}

// 写数据
static void lcd_write_data(uint8_t data)
{
    gpio_set_level(LCD_RS_GPIO, 1);
    lcd_spi_write_byte(data);
}

// 写16位数据
static void lcd_write_data_16(uint16_t data)
{
    lcd_write_data((data >> 8) & 0xFF);
    lcd_write_data(data & 0xFF);
}

// 设置显示窗口
static void lcd_set_window(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end)
{
    lcd_write_cmd(0x2A);
    lcd_write_data_16(x_start);
    lcd_write_data_16(x_end);

    lcd_write_cmd(0x2B);
    lcd_write_data_16(y_start);
    lcd_write_data_16(y_end);

    lcd_write_cmd(0x2C);
}

// ST7789V初始化
static void lcd_init(void)
{
    lcd_gpio_init();

    vTaskDelay(200 / portTICK_PERIOD_MS);

    lcd_write_cmd(0x11);
    vTaskDelay(120 / portTICK_PERIOD_MS);

    lcd_write_cmd(0x36);
    lcd_write_data(0x00);

    lcd_write_cmd(0x3A);
    lcd_write_data(0x05);

    lcd_write_cmd(0x21);

    lcd_write_cmd(0x29);

    ESP_LOGI(TAG, "LCD initialized");
}

// LVGL flush回调
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;

    lcd_set_window(offsetx1, offsety1, offsetx2, offsety2);

    uint32_t pixel_num = (offsetx2 - offsetx1 + 1) * (offsety2 - offsety1 + 1);
    uint16_t *pixel_data = (uint16_t *)px_map;

    for (uint32_t i = 0; i < pixel_num; i++) {
        lcd_write_data_16(pixel_data[i]);
    }

    lv_display_flush_ready(disp);
}

static void increase_lvgl_tick(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

void lvgl_init(void)
{
    lv_init();

    display = lv_display_create(LCD_H_RES, LCD_V_RES);

    size_t draw_buffer_sz = LCD_H_RES * LVGL_DRAW_BUF_LINES * sizeof(lv_color16_t);

    void *buf1 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_DMA);
    assert(buf1);
    void *buf2 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_DMA);
    assert(buf2);

    lv_display_set_buffers(display, buf1, buf2, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, lvgl_flush_cb);

    const esp_timer_create_args_t lvgl_tick_timer_args = {.callback = &increase_lvgl_tick, .name = "lvgl_tick"};
    esp_timer_handle_t lvgl_tick_timer = NULL;
    esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer);
    esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000);
}

// LVGL任务
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

// 编码器处理任务（20定位20脉冲，每个脉冲都响应）
static void encoder_task(void *arg)
{
    ESP_LOGI(TAG, "Encoder task started (20/20 pulse)");
    
    while (1) {
        ec11_event_t event = encoder_get_event();
        
        switch (event) {
            case EC11_EVENT_CW:
                ESP_LOGI(TAG, "Encoder: CW (next screen)");
                ui_next_screen();
                break;
                
            case EC11_EVENT_CCW:
                ESP_LOGI(TAG, "Encoder: CCW (prev screen)");
                ui_prev_screen();
                break;
                
            case EC11_EVENT_PRESS:
                ESP_LOGI(TAG, "Encoder: Button pressed");
                // 按键可以执行确认操作
                break;
                
            default:
                break;
        }
        
        vTaskDelay(5 / portTICK_PERIOD_MS);  // 5ms轮询
    }
}

// 时间更新任务
static void time_update_task(void *arg)
{
    ESP_LOGI(TAG, "Time update task started");
    
    while (1) {
        ui_update_time();
        vTaskDelay(1000 / portTICK_PERIOD_MS);  // 每秒更新一次
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "================================");
    ESP_LOGI(TAG, "ST7789 + LVGL + EC11 (20/20) Demo");
    ESP_LOGI(TAG, "================================");
    
    // 初始化LCD
    lcd_init();
    
    // 初始化LVGL
    lvgl_init();
    
    // 初始化UI
    ui_init();
    
    // 初始化编码器
    encoder_init();
    
    // 创建任务
    xTaskCreate(lvgl_port_task, "LVGL", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL);
    xTaskCreate(encoder_task, "Encoder", 4096, NULL, 2, NULL);
    xTaskCreate(time_update_task, "TimeUpdate", 2048, NULL, 1, NULL);
    
    ESP_LOGI(TAG, "All tasks created");
    
    // 主循环
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

// 空实现防止警告
void lv_example(void) {}
