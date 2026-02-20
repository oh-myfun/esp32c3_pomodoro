#include "driver/gpio.h"
#include "driver/spi_master.h"
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
#include "wifi_manager.h"

static const char *TAG = "MAIN";

// ===================== LCD引脚配置 (硬件SPI) =====================
#define LCD_RS_GPIO GPIO_NUM_10   // DC/RS
#define LCD_SCK_GPIO GPIO_NUM_6   // SCK
#define LCD_SDA_GPIO GPIO_NUM_7   // SDA (MOSI)
#define LCD_SPI_HOST SPI2_HOST    // 使用SPI2
#define LCD_SPI_FREQ_HZ 60000000  // 60MHz
// =====================================================

#define LCD_V_RES 240
#define LCD_H_RES 240
#define LVGL_DRAW_BUF_LINES 60
#define LVGL_TICK_PERIOD_MS 2
#define LVGL_TASK_MAX_DELAY_MS 500
#define LVGL_TASK_MIN_DELAY_MS 1000 / CONFIG_FREERTOS_HZ
#define LVGL_TASK_STACK_SIZE (8 * 1024)
#define LVGL_TASK_PRIORITY 2

static _lock_t lvgl_api_lock;
static lv_display_t *display = NULL;
static spi_device_handle_t lcd_spi = NULL;

// GPIO初始化 (仅DC引脚)
static void lcd_gpio_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LCD_RS_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io_conf);
    gpio_set_level(LCD_RS_GPIO, 0);
}

// 硬件SPI初始化
static void lcd_spi_init(void)
{
    spi_bus_config_t buscfg = {
        .miso_io_num = -1,  // 不使用MISO
        .mosi_io_num = LCD_SDA_GPIO,
        .sclk_io_num = LCD_SCK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 240 * 240 * 2 + 8,  // 最大传输240x240x2字节 + 指令开销
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = LCD_SPI_FREQ_HZ,
        .mode = 0,  // CPOL=0, CPHA=0
        .spics_io_num = -1,  // 不使用CS
        .queue_size = 7,
        .pre_cb = NULL,
    };

    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device(LCD_SPI_HOST, &devcfg, &lcd_spi));
    ESP_LOGI(TAG, "Hardware SPI initialized: %d MHz", LCD_SPI_FREQ_HZ / 1000000);
}

// 硬件SPI发送数据
static void lcd_spi_write_data(const uint8_t *data, size_t len, bool is_cmd)
{
    gpio_set_level(LCD_RS_GPIO, is_cmd ? 0 : 1);
    
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(lcd_spi, &t));
}

// 写指令
static void lcd_write_cmd(uint8_t cmd)
{
    lcd_spi_write_data(&cmd, 1, true);
}

// 写数据
static void lcd_write_data(uint8_t data)
{
    lcd_spi_write_data(&data, 1, false);
}

// 写16位数据（大端格式，与ST7789一致）
static void lcd_write_data_16(uint16_t data)
{
    uint8_t buf[2] = {(data >> 8) & 0xFF, data & 0xFF};
    lcd_spi_write_data(buf, 2, false);
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
    lcd_spi_init();

    vTaskDelay(200 / portTICK_PERIOD_MS);

    lcd_write_cmd(0x11);
    vTaskDelay(120 / portTICK_PERIOD_MS);

    lcd_write_cmd(0x36);
    lcd_write_data(0x00);

    lcd_write_cmd(0x3A);
    lcd_write_data(0x05);

    lcd_write_cmd(0x21);

    lcd_write_cmd(0x29);

    ESP_LOGI(TAG, "LCD initialized with hardware SPI");
}

// LVGL flush回调 - DMA传输（交换字节顺序）
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;

    lcd_set_window(offsetx1, offsety1, offsetx2, offsety2);

    uint32_t pixel_num = (offsetx2 - offsetx1 + 1) * (offsety2 - offsety1 + 1);
    uint16_t *pixel_data = (uint16_t *)px_map;

    // 交换字节顺序（小端->大端，与ST7789一致）
    for (uint32_t i = 0; i < pixel_num; i++) {
        pixel_data[i] = (pixel_data[i] >> 8) | (pixel_data[i] << 8);
    }

    // DC置高(数据模式)
    gpio_set_level(LCD_RS_GPIO, 1);

    // DMA传输
    spi_transaction_t t = {
        .length = pixel_num * 16,
        .tx_buffer = px_map,
    };
    spi_device_polling_transmit(lcd_spi, &t);

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

// 编码器处理任务
static void encoder_task(void *arg)
{
    ESP_LOGI(TAG, "Encoder task started");

    while (1) {
        ec11_event_t event = encoder_get_event();

        if (event != EC11_EVENT_NONE) {
            _lock_acquire(&lvgl_api_lock);

            ui_screen_id_t screen = ui_get_current_screen();
            settings_mode_t mode = ui_get_settings_mode();

            // WiFi列表界面
            if (screen == UI_SCREEN_WIFI_LIST) {
                switch (event) {
                    case EC11_EVENT_CW:
                        ui_wifi_list_select_next();
                        break;
                    case EC11_EVENT_CCW:
                        ui_wifi_list_select_prev();
                        break;
                    case EC11_EVENT_PRESS:
                        ui_switch_screen(UI_SCREEN_SETTINGS);
                        break;
                    default:
                        break;
                }
            }
            // 密码输入界面
            else if (screen == UI_SCREEN_PASSWORD_INPUT) {
                switch (event) {
                    case EC11_EVENT_CW:
                        ui_password_input_char_next();
                        break;
                    case EC11_EVENT_CCW:
                        ui_password_input_char_prev();
                        break;
                    case EC11_EVENT_PRESS:
                        ui_password_input_cancel();
                        break;
                    default:
                        break;
                }
            }
            // 设置界面 - 滚动切换回主界面
            else if (screen == UI_SCREEN_SETTINGS && mode == SETTINGS_MODE_IDLE) {
                switch (event) {
                    case EC11_EVENT_CW:
                    case EC11_EVENT_CCW:
                        ui_switch_screen(UI_SCREEN_MAIN);
                        break;
                    default:
                        break;
                }
            }
            // 设置界面 - 设置模式
            else if (screen == UI_SCREEN_SETTINGS && mode != SETTINGS_MODE_IDLE) {
                switch (event) {
                    case EC11_EVENT_CW:
                        if (mode == SETTINGS_MODE_SELECT) {
                            ui_settings_select_next();
                        } else if (mode == SETTINGS_MODE_ADJUST) {
                            ui_settings_adjust_up();
                        }
                        break;
                    case EC11_EVENT_CCW:
                        if (mode == SETTINGS_MODE_SELECT) {
                            ui_settings_select_prev();
                        } else if (mode == SETTINGS_MODE_ADJUST) {
                            ui_settings_adjust_down();
                        }
                        break;
                    case EC11_EVENT_PRESS:
                        // 设置界面按编码器键返回主界面
                        ui_exit_settings();
                        break;
                    default:
                        break;
                }
            }
            // 主界面 - 滚动进入设置界面
            else if (screen == UI_SCREEN_MAIN) {
                switch (event) {
                    case EC11_EVENT_CW:
                    case EC11_EVENT_CCW:
                        ui_switch_screen(UI_SCREEN_SETTINGS);
                        break;
                    default:
                        break;
                }
            }

            _lock_release(&lvgl_api_lock);
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// 设置按键处理任务
static void settings_button_task(void *arg)
{
    ESP_LOGI(TAG, "Settings button task started");

    while (1) {
        if (settings_button_get_event()) {
            _lock_acquire(&lvgl_api_lock);

            ui_screen_id_t screen = ui_get_current_screen();

            if (screen == UI_SCREEN_WIFI_LIST) {
                ui_wifi_list_confirm();
            } else if (screen == UI_SCREEN_PASSWORD_INPUT) {
                ui_password_input_add_char();
            } else {
                ui_enter_settings();
            }

            _lock_release(&lvgl_api_lock);
        }

        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

// 时间更新任务（500ms更新一次，使秒数显示更流畅）
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

// WiFi状态更新任务
static void wifi_status_task(void *arg)
{
    ESP_LOGI(TAG, "WiFi status task started");

    while (1) {
        _lock_acquire(&lvgl_api_lock);

        // WiFi列表界面刷新
        if (ui_get_current_screen() == UI_SCREEN_WIFI_LIST) {
            ui_wifi_list_refresh();
            
            // 扫描期间更频繁刷新
            if (!wifi_manager_is_scan_done()) {
                _lock_release(&lvgl_api_lock);
                vTaskDelay(100 / portTICK_PERIOD_MS);
                continue;
            }
        }

        // 主界面WiFi状态
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

    // 初始化LCD
    lcd_init();

    // 初始化LVGL
    lvgl_init();

    // 初始化UI
    ui_init();

    // 初始化编码器
    encoder_init();

    // 初始化WiFi
    wifi_manager_init();

    // 创建任务
    xTaskCreate(lvgl_port_task, "LVGL", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL);
    xTaskCreate(encoder_task, "Encoder", 4096, NULL, 2, NULL);
    xTaskCreate(time_update_task, "TimeUpdate", 4096, NULL, 1, NULL);
    xTaskCreate(settings_button_task, "SettingsBtn", 4096, NULL, 2, NULL);
    xTaskCreate(wifi_status_task, "WiFiStatus", 4096, NULL, 1, NULL);

    ESP_LOGI(TAG, "All tasks created");
    
    // 主循环
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

// 空实现防止警告
void lv_example(void) {}
