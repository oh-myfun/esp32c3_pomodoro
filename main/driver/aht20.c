#include "aht20.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "AHT20";

#define AHT20_ADDR          0x38
#define AHT20_CMD_TRIGGER   0xAC
#define AHT20_CMD_INIT      0xBE
#define AHT20_CMD_SOFTRESET 0xBA

#define I2C_SCL_GPIO  GPIO_NUM_0
#define I2C_SDA_GPIO  GPIO_NUM_1
#define I2C_FREQ_HZ   400000

static i2c_master_bus_handle_t bus_handle = NULL;
static i2c_master_dev_handle_t dev_handle = NULL;
static bool initialized = false;

void aht20_init(void)
{
    if (initialized) return;

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
    };
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "I2C bus init failed (no sensor?): %s", esp_err_to_name(ret));
        return;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AHT20_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "AHT20 device add failed: %s", esp_err_to_name(ret));
        return;
    }

    /* Wait for sensor boot */
    vTaskDelay(pdMS_TO_TICKS(40));

    /* Soft reset — use timeout to avoid hanging on bad bus */
    uint8_t reset_cmd = AHT20_CMD_SOFTRESET;
    ret = i2c_master_transmit(dev_handle, &reset_cmd, 1, 100);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "AHT20 reset failed: %s", esp_err_to_name(ret));
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    /* Calibration: enable CRC + output (status bit[3]=1, bit[2]=1) */
    uint8_t init_cmd[] = {AHT20_CMD_INIT, 0x08, 0x00};
    ret = i2c_master_transmit(dev_handle, init_cmd, sizeof(init_cmd), 100);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "AHT20 init failed: %s", esp_err_to_name(ret));
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    initialized = true;
    ESP_LOGI(TAG, "AHT20 initialized (SCL=%d, SDA=%d)", I2C_SCL_GPIO, I2C_SDA_GPIO);
}

bool aht20_is_available(void)
{
    return initialized;
}

bool aht20_read(float *temperature, float *humidity)
{
    if (!initialized) return false;

    /* Trigger measurement */
    uint8_t trigger_cmd[] = {AHT20_CMD_TRIGGER, 0x33, 0x00};
    esp_err_t ret = i2c_master_transmit(dev_handle, trigger_cmd, sizeof(trigger_cmd), 200);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "Trigger failed: %s", esp_err_to_name(ret));
        return false;
    }

    /* Wait for measurement (typical 80ms) */
    vTaskDelay(pdMS_TO_TICKS(80));

    /* Read 6 bytes: status, H[19:12], H[11:4], H[3:0]+T[19:16], T[15:8], T[7:0] */
    uint8_t data[6];
    ret = i2c_master_receive(dev_handle, data, sizeof(data), 200);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "Read failed: %s", esp_err_to_name(ret));
        return false;
    }

    /* Check busy bit */
    if (data[0] & 0x80) {
        ESP_LOGD(TAG, "Sensor busy");
        return false;
    }

    /* Humidity: 20-bit value */
    uint32_t hum_raw = ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | ((uint32_t)data[3] >> 4);
    *humidity = (float)hum_raw / 1048576.0f * 100.0f;

    /* Temperature: 20-bit value */
    uint32_t temp_raw = (((uint32_t)data[3] & 0x0F) << 16) | ((uint32_t)data[4] << 8) | (uint32_t)data[5];
    *temperature = (float)temp_raw / 1048576.0f * 200.0f - 50.0f;

    return true;
}

i2c_master_bus_handle_t aht20_get_i2c_bus(void)
{
    return bus_handle;
}
