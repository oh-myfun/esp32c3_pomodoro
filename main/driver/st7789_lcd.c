#include "st7789_lcd.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ST7789_LCD";

#define LCD_RS_GPIO   GPIO_NUM_10
#define LCD_SCK_GPIO  GPIO_NUM_6
#define LCD_SDA_GPIO  GPIO_NUM_7
#define LCD_RST_GPIO  GPIO_NUM_2
#define LCD_SPI_HOST  SPI2_HOST
#define LCD_SPI_FREQ  60000000

#define LCD_V_RES 240
#define LCD_H_RES 240

static spi_device_handle_t lcd_spi = NULL;
static int flush_count = 0;

static void lcd_gpio_init(void)
{
    ESP_LOGI(TAG, "Configuring GPIO: RS=%d, RST=%d", LCD_RS_GPIO, LCD_RST_GPIO);

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LCD_RS_GPIO) | (1ULL << LCD_RST_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    esp_err_t ret = gpio_config(&io_conf);
    ESP_LOGI(TAG, "gpio_config: %s", esp_err_to_name(ret));

    gpio_set_level(LCD_RS_GPIO, 0);
    gpio_set_level(LCD_RST_GPIO, 1);
    ESP_LOGI(TAG, "RS=0, RST=1 (initial state)");
}

static void lcd_hw_reset(void)
{
    ESP_LOGI(TAG, "HW reset: RST HIGH (10ms)");
    gpio_set_level(LCD_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "HW reset: RST LOW (10ms)");
    gpio_set_level(LCD_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "HW reset: RST HIGH (wait 120ms)");
    gpio_set_level(LCD_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_LOGI(TAG, "HW reset done, RST level=%d", gpio_get_level(LCD_RST_GPIO));
}

static void lcd_spi_init(void)
{
    spi_bus_config_t buscfg = {
        .miso_io_num = -1,
        .mosi_io_num = LCD_SDA_GPIO,
        .sclk_io_num = LCD_SCK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 240 * 240 * 2 + 8,
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = LCD_SPI_FREQ,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 7,
        .pre_cb = NULL,
    };

    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device(LCD_SPI_HOST, &devcfg, &lcd_spi));
    ESP_LOGI(TAG, "SPI initialized: %d MHz", LCD_SPI_FREQ / 1000000);
}

static void lcd_write_data(const uint8_t *data, size_t len, bool is_cmd)
{
    gpio_set_level(LCD_RS_GPIO, is_cmd ? 0 : 1);
    
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(lcd_spi, &t));
}

static void lcd_write_cmd(uint8_t cmd)
{
    lcd_write_data(&cmd, 1, true);
}

static void lcd_write_byte(uint8_t data)
{
    lcd_write_data(&data, 1, false);
}

static void lcd_write_data_16(uint16_t data)
{
    uint8_t buf[2] = {(data >> 8) & 0xFF, data & 0xFF};
    lcd_write_data(buf, 2, false);
}

void st7789_lcd_init(void)
{
    ESP_LOGI(TAG, "=== LCD init start ===");

    lcd_gpio_init();
    lcd_spi_init();

    ESP_LOGI(TAG, "Waiting 200ms for LCD power-on reset...");
    vTaskDelay(pdMS_TO_TICKS(200));

    lcd_hw_reset();

    ESP_LOGI(TAG, "Sending SLPOUT (0x11)");
    lcd_write_cmd(0x11);
    ESP_LOGI(TAG, "Waiting 120ms after SLPOUT...");
    vTaskDelay(pdMS_TO_TICKS(120));

    ESP_LOGI(TAG, "Sending MADCTL (0x36) = 0x00");
    lcd_write_cmd(0x36);
    lcd_write_byte(0x00);

    ESP_LOGI(TAG, "Sending COLMOD (0x3A) = 0x05");
    lcd_write_cmd(0x3A);
    lcd_write_byte(0x05);

    ESP_LOGI(TAG, "Sending INVON (0x21)");
    lcd_write_cmd(0x21);

    ESP_LOGI(TAG, "Sending DISPON (0x29)");
    lcd_write_cmd(0x29);

    ESP_LOGI(TAG, "=== LCD init complete ===");
}

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

void st7789_lcd_reset(void)
{
    lcd_hw_reset();

    lcd_write_cmd(0x11);
    vTaskDelay(pdMS_TO_TICKS(120));

    lcd_write_cmd(0x36);
    lcd_write_byte(0x00);

    lcd_write_cmd(0x3A);
    lcd_write_byte(0x05);

    lcd_write_cmd(0x21);
    lcd_write_cmd(0x29);

    ESP_LOGI(TAG, "LCD reset complete");
}

void st7789_lcd_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    if (flush_count < 5) {
        ESP_LOGI(TAG, "flush #%d: (%d,%d)-(%d,%d)", flush_count,
                 area->x1, area->y1, area->x2, area->y2);
    }
    flush_count++;

    int32_t x1 = area->x1;
    int32_t x2 = area->x2;
    int32_t y1 = area->y1;
    int32_t y2 = area->y2;

    lcd_set_window(x1, y1, x2, y2);

    uint32_t pixel_num = (x2 - x1 + 1) * (y2 - y1 + 1);
    uint16_t *pixel_data = (uint16_t *)px_map;

    for (uint32_t i = 0; i < pixel_num; i++) {
        pixel_data[i] = (pixel_data[i] >> 8) | (pixel_data[i] << 8);
    }

    gpio_set_level(LCD_RS_GPIO, 1);

    spi_transaction_t t = {
        .length = pixel_num * 16,
        .tx_buffer = px_map,
    };
    spi_device_polling_transmit(lcd_spi, &t);

    lv_display_flush_ready(disp);
}
