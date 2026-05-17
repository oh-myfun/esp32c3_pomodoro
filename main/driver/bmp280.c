#include "bmp280.h"
#include "driver/i2c_master.h"
#include "aht20.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BMP280";

#define BMP280_ADDR         0x77

/* Registers */
#define BMP280_REG_ID       0xD0
#define BMP280_REG_RESET    0xE0
#define BMP280_REG_CTRL_MEAS 0xF4
#define BMP280_REG_CONFIG   0xF5
#define BMP280_REG_PRESS    0xF7
#define BMP280_REG_TEMP     0xFA
#define BMP280_REG_DIG_T1   0x88
#define BMP280_REG_DIG_P1   0x8E

/* Same I2C bus as AHT20 (already initialized) */
#define I2C_SCL_GPIO  GPIO_NUM_0
#define I2C_SDA_GPIO  GPIO_NUM_1
#define I2C_FREQ_HZ   400000

static i2c_master_bus_handle_t bus_handle = NULL;
static i2c_master_dev_handle_t dev_handle = NULL;
static bool initialized = false;

/* Calibration data */
static uint16_t dig_T1;
static int16_t  dig_T2, dig_T3;
static uint16_t dig_P1;
static int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
static int32_t  t_fine;

static void read_regs(uint8_t reg, uint8_t *buf, size_t len)
{
    i2c_master_transmit_receive(dev_handle, &reg, 1, buf, len, -1);
}

static void write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[] = {reg, val};
    i2c_master_transmit(dev_handle, buf, sizeof(buf), -1);
}

void bmp280_init(void)
{
    if (initialized) return;

    /* Reuse I2C bus created by AHT20 */
    bus_handle = aht20_get_i2c_bus();
    if (bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus not available (init AHT20 first)");
        return;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BMP280_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle));

    /* Check chip ID — try both possible addresses */
    uint8_t id = 0;
    read_regs(BMP280_REG_ID, &id, 1);
    if (id != 0x58) {
        ESP_LOGW(TAG, "No BMP280 at 0x76 (got ID=0x%02X), trying 0x77...", id);
        /* Try alternate address */
        i2c_device_config_t dev_cfg_alt = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = 0x77,
            .scl_speed_hz = I2C_FREQ_HZ,
        };
        i2c_master_bus_add_device(bus_handle, &dev_cfg_alt, &dev_handle);
        read_regs(BMP280_REG_ID, &id, 1);
        if (id != 0x58) {
            ESP_LOGE(TAG, "No BMP280 found (ID=0x%02X at 0x77)", id);
            return;
        }
        ESP_LOGI(TAG, "Found BMP280 at 0x77");
    }

    /* Soft reset */
    write_reg(BMP280_REG_RESET, 0xB6);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Read calibration data */
    uint8_t cal[24];
    read_regs(BMP280_REG_DIG_T1, cal, 24);

    dig_T1 = (uint16_t)(cal[1] << 8) | cal[0];
    dig_T2 = (int16_t)(cal[3] << 8)  | cal[2];
    dig_T3 = (int16_t)(cal[5] << 8)  | cal[4];
    dig_P1 = (uint16_t)(cal[7] << 8) | cal[6];
    dig_P2 = (int16_t)(cal[9] << 8)  | cal[8];
    dig_P3 = (int16_t)(cal[11] << 8) | cal[10];
    dig_P4 = (int16_t)(cal[13] << 8) | cal[12];
    dig_P5 = (int16_t)(cal[15] << 8) | cal[14];
    dig_P6 = (int16_t)(cal[17] << 8) | cal[16];
    dig_P7 = (int16_t)(cal[19] << 8) | cal[18];
    dig_P8 = (int16_t)(cal[21] << 8) | cal[20];
    dig_P9 = (int16_t)(cal[23] << 8) | cal[22];

    /* Config: standby 0.5ms, filter off */
    write_reg(BMP280_REG_CONFIG, 0x00);

    /* Ctrl_meas: normal mode, oversampling x1 for temp and pressure */
    write_reg(BMP280_REG_CTRL_MEAS, 0x27);

    initialized = true;
    ESP_LOGI(TAG, "BMP280 initialized (ID=0x%02X)", id);
}

bool bmp280_is_available(void)
{
    return initialized;
}

/* Bosch compensation formulas from datasheet */
static int32_t compensate_temp(int32_t adc_T)
{
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) * ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) * ((int32_t)dig_T3)) >> 14;
    t_fine = var1 + var2;
    return (t_fine * 5 + 128) >> 8;
}

static uint32_t compensate_press(int32_t adc_P)
{
    int64_t var1 = ((int64_t)t_fine) - 128000;
    int64_t var2 = var1 * var1 * (int64_t)dig_P6;
    var2 = var2 + ((var1 * (int64_t)dig_P5) << 17);
    var2 = var2 + (((int64_t)dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)dig_P3) >> 8) + ((var1 * (int64_t)dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1) * ((int64_t)dig_P1)) >> 33;
    if (var1 == 0) return 0;

    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)dig_P7) << 4);
    return (uint32_t)p;
}

bool bmp280_read(float *temperature, float *pressure_hpa)
{
    if (!initialized) return false;

    uint8_t data[6];
    read_regs(BMP280_REG_PRESS, data, 6);

    int32_t adc_P = ((int32_t)data[0] << 12) | ((int32_t)data[1] << 4) | ((int32_t)data[2] >> 4);
    int32_t adc_T = ((int32_t)data[3] << 12) | ((int32_t)data[4] << 4) | ((int32_t)data[5] >> 4);

    if (adc_T == 0) return false;

    *temperature = compensate_temp(adc_T) / 100.0f;
    *pressure_hpa = compensate_press(adc_P) / 25600.0f;

    return true;
}
