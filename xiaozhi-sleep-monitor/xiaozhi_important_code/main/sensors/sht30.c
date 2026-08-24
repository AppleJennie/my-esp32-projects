/**
 * SHT30 驱动实现（新版 i2c_master API, I2C_NUM_1 独立总线）
 *
 * 使用 i2c_new_master_bus + i2c_master_transmit，彻底消除旧 API 链接冲突
 */
#include "sht30.h"
#include "app_role_config.h"
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <string.h>

static const char *TAG = "SHT30";

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;
static bool s_ready = false;

/* CRC8: x^8 + x^5 + x^4 + 1 (0x31) */
static uint8_t crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0xFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++)
            crc = (crc & 0x80) ? ((crc << 1) ^ 0x31) : (crc << 1);
    }
    return crc;
}

esp_err_t sht30_init(void)
{
    if (s_ready) return ESP_OK;

    /* 1. 创建独立 I2C1 总线 */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = SHT30_I2C_PORT,
        .sda_io_num = SHT30_I2C_SDA_GPIO,
        .scl_io_num = SHT30_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 2. 添加 SHT30 设备 */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHT30_I2C_ADDR,
        .scl_speed_hz = SHT30_I2C_FREQ,
    };
    ret = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bus_add_device: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 3. 软复位 */
    uint8_t rst[2] = {0x30, 0xA2};
    i2c_master_transmit(s_dev, rst, 2, 100);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* 4. 测试读取 */
    sht30_data_t t;
    ret = sht30_read(&t);
    if (ret == ESP_OK) {
        s_ready = true;
        ESP_LOGI(TAG, "Init OK I2C%d SDA=GPIO%d SCL=GPIO%d T=%.1f°C H=%.1f%%",
                 SHT30_I2C_PORT, SHT30_I2C_SDA_GPIO, SHT30_I2C_SCL_GPIO,
                 t.temperature, t.humidity);
    } else {
        ESP_LOGW(TAG, "Read test failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t sht30_soft_reset(void)
{
    uint8_t cmd[2] = {0x30, 0xA2};
    return s_dev ? i2c_master_transmit(s_dev, cmd, 2, 100) : ESP_ERR_INVALID_STATE;
}

esp_err_t sht30_read(sht30_data_t *out)
{
    if (!out || !s_dev) return ESP_ERR_INVALID_ARG;

    /* 发送测量命令 */
    uint8_t cmd[2] = {0x2C, 0x06};
    esp_err_t r = i2c_master_transmit(s_dev, cmd, 2, 100);
    if (r != ESP_OK) return r;
    vTaskDelay(pdMS_TO_TICKS(20));

    /* 读 6 字节 */
    uint8_t data[6] = {0};
    r = i2c_master_receive(s_dev, data, 6, 100);
    if (r != ESP_OK) return r;

    if (crc8(data, 2) != data[2] || crc8(&data[3], 2) != data[5])
        return ESP_ERR_INVALID_CRC;

    uint16_t tr = ((uint16_t)data[0] << 8) | data[1];
    uint16_t hr = ((uint16_t)data[3] << 8) | data[4];
    out->temperature = -45.0f + 175.0f * ((float)tr / 65535.0f);
    out->humidity    = 100.0f * ((float)hr / 65535.0f);
    return ESP_OK;
}

esp_err_t sht30_read_status(uint16_t *status)
{
    if (!status || !s_dev) return ESP_ERR_INVALID_ARG;
    uint8_t cmd[2] = {0xF3, 0x2D};
    esp_err_t r = i2c_master_transmit(s_dev, cmd, 2, 100);
    if (r != ESP_OK) return r;
    vTaskDelay(pdMS_TO_TICKS(5));
    uint8_t data[3];
    r = i2c_master_receive(s_dev, data, 3, 100);
    if (r != ESP_OK) return r;
    if (crc8(data, 2) != data[2]) return ESP_ERR_INVALID_CRC;
    *status = ((uint16_t)data[0] << 8) | data[1];
    return ESP_OK;
}
