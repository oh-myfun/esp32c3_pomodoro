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
#define LCD_RST_GPIO  GPIO_NUM_3
#define LCD_SPI_HOST  SPI2_HOST
#define LCD_SPI_FREQ  60000000

#define LCD_V_RES 240
#define LCD_H_RES 240

static spi_device_handle_t lcd_spi = NULL;

static void lcd_gpio_init(void)
{
    ESP_LOGI(TAG, "Configuring GPIO: RS=%d, RST=%d", LCD_RS_GPIO, LCD_RST_GPIO);

    /* Pre-set output levels BEFORE enabling output mode to avoid glitches */
    gpio_set_level(LCD_RST_GPIO, 1);
    gpio_set_level(LCD_RS_GPIO, 0);

    /* RS (DC) pin: pure output for SPI command/data selection */
    gpio_config_t rs_conf = {
        .pin_bit_mask = 1ULL << LCD_RS_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&rs_conf);

    /* RST pin: output with pull-up */
    gpio_config_t rst_conf = {
        .pin_bit_mask = 1ULL << LCD_RST_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&rst_conf);

    ESP_LOGI(TAG, "RS=%d, RST=%d", gpio_get_level(LCD_RS_GPIO), gpio_get_level(LCD_RST_GPIO));
}

static void lcd_hw_reset(void)
{
    /* HW reset pulse matching JLX130-026-PN reference code timing.
       After ESP32 warm reboot the LCD SPI receiver may be desynchronized
       from garbage clocking during boot; only HW reset can recover it. */
    ESP_LOGI(TAG, "HW reset pulse");
    gpio_set_level(LCD_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(LCD_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
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

static void lcd_write_bytes(const uint8_t *data, size_t len)
{
    lcd_write_data(data, len, false);
}

static void lcd_write_data_16(uint16_t data)
{
    uint8_t buf[2] = {(data >> 8) & 0xFF, data & 0xFF};
    lcd_write_data(buf, 2, false);
}

/* Send vendor init commands (from module reference code) */
static void lcd_send_init_sequence(void)
{
    /* SLPOUT: exit sleep mode (200 ms per reference code) */
    lcd_write_cmd(0x11);
    vTaskDelay(pdMS_TO_TICKS(200));

    /* MADCTL: display direction and RGB order */
    lcd_write_cmd(0x36);
    lcd_write_byte(0x00);

    /* DISFUNC: display function setting (column/row scan order) */
    lcd_write_cmd(0xB6);
    lcd_write_byte(0x0A);
    lcd_write_byte(0xA2);

    /* COLMOD: 16bit/pixel RGB565 */
    lcd_write_cmd(0x3A);
    lcd_write_byte(0x05);

    /* PORCTRL: porch setting */
    lcd_write_cmd(0xB2);
    lcd_write_bytes((const uint8_t[]){0x0C, 0x0C, 0x00, 0x33, 0x33}, 5);

    /* GCTRL: gate control */
    lcd_write_cmd(0xB7);
    lcd_write_byte(0x35);

    /* VCOMS: VCOM setting */
    lcd_write_cmd(0xBB);
    lcd_write_byte(0x19);

    /* LCMCTRL: LCM control */
    lcd_write_cmd(0xC0);
    lcd_write_byte(0x2C);

    /* VDVVRHEN: VDV and VRH command enable */
    lcd_write_cmd(0xC2);
    lcd_write_byte(0x01);

    /* VRHS: VRH set */
    lcd_write_cmd(0xC3);
    lcd_write_byte(0x12);

    /* VDVS: VDV set */
    lcd_write_cmd(0xC4);
    lcd_write_byte(0x20);

    /* FRCTRL2: frame rate control (60Hz) */
    lcd_write_cmd(0xC6);
    lcd_write_byte(0x0F);

    /* PWCTRL1: power control */
    lcd_write_cmd(0xD0);
    lcd_write_bytes((const uint8_t[]){0xA4, 0xA1}, 2);

    /* PVGAMCTRL: positive voltage gamma correction */
    lcd_write_cmd(0xE0);
    lcd_write_bytes((const uint8_t[]){0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B,
                                       0x3F, 0x54, 0x4C, 0x18, 0x0D, 0x0B,
                                       0x1F, 0x23}, 14);

    /* NVGAMCTRL: negative voltage gamma correction */
    lcd_write_cmd(0xE1);
    lcd_write_bytes((const uint8_t[]){0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C,
                                       0x3F, 0x44, 0x51, 0x2F, 0x1F, 0x1F,
                                       0x20, 0x23}, 14);

    /* INVON: inversion on */
    lcd_write_cmd(0x21);

    /* DISPON: display on */
    lcd_write_cmd(0x29);
}

void st7789_lcd_init(void)
{
    ESP_LOGI(TAG, "=== LCD init start ===");

    /* 1. GPIO first (RST pin available for reset) */
    lcd_gpio_init();

    /* 2. Reset LCD BEFORE SPI init so it ignores any SPI boot glitches */
    lcd_hw_reset();

    /* 3. Now safe to start SPI — LCD is in known reset state */
    lcd_spi_init();

    /* 4. Send init command sequence */
    lcd_send_init_sequence();

    ESP_LOGI(TAG, "=== LCD init complete ===");
}

void st7789_lcd_reinit(void)
{
    ESP_LOGI(TAG, "=== LCD reinit (screen reset) ===");

    /* 1. Reset LCD first so it ignores SPI during teardown/reinit */
    lcd_hw_reset();

    /* 2. Tear down and recreate SPI to ensure clean state */
    if (lcd_spi) {
        spi_bus_remove_device(lcd_spi);
        lcd_spi = NULL;
    }
    spi_bus_free(LCD_SPI_HOST);

    lcd_spi_init();

    /* 3. Full init sequence on fresh LCD */
    lcd_send_init_sequence();

    ESP_LOGI(TAG, "=== LCD reinit complete ===");
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

void st7789_lcd_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
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
