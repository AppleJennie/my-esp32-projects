/**
 * SHT30 驱动实现（适配 ESP-IDF v5.4 + ATK DNESP32S3 I2C0 共享总线）
 */
#include "sht30.h"
#include <esp_log.h>
#include <string.h>

static const char *TAG = "SHT30";

/* CRC8 校验，多项式 x^8 + x^5 + x^4 + 1 (0x31) */
static uint8_t sht30_crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0xFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            crc = (crc & 0x80) ? ((crc << 1) ^ 0x31) : (crc << 1);
        }
    }
    return crc;
}

esp_err_t sht30_init(void)
{
    /* I2C0 总线已由 Board 层初始化（i2c_new_master_bus），此处仅验证传感器 */

    /* 先发送软复位 */
    esp_err_t ret = sht30_soft_reset();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Soft reset failed, sensor may not be present");
        return ret;
    }

    /* 验证：尝试读一次数据 */
    sht30_data_t test;
    ret = sht30_read(&test);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SHT30 init OK, I2C0 addr=0x%02X, T=%.1f°C H=%.1f%%",
                 SHT30_I2C_ADDR, test.temperature, test.humidity);
    } else {
        ESP_LOGW(TAG, "SHT30 read test failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t sht30_soft_reset(void)
{
    uint8_t cmd[2] = {SHT30_CMD_SOFT_RESET >> 8, SHT30_CMD_SOFT_RESET & 0xFF};
    i2c_cmd_handle_t cmd_handle = i2c_cmd_link_create();
    i2c_master_start(cmd_handle);
    i2c_master_write_byte(cmd_handle, (SHT30_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd_handle, cmd, 2, true);
    i2c_master_stop(cmd_handle);
    esp_err_t ret = i2c_master_cmd_begin(SHT30_I2C_PORT, cmd_handle, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd_handle);
    vTaskDelay(pdMS_TO_TICKS(10));
    return ret;
}

esp_err_t sht30_read(sht30_data_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;

    /* 发送测量命令：单次测量，高重复性，时钟拉伸 */
    uint8_t cmd[2] = {SHT30_CMD_MEAS_HIGHREP_STRETCH >> 8,
                      SHT30_CMD_MEAS_HIGHREP_STRETCH & 0xFF};
    i2c_cmd_handle_t cmd_handle = i2c_cmd_link_create();
    i2c_master_start(cmd_handle);
    i2c_master_write_byte(cmd_handle, (SHT30_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd_handle, cmd, 2, true);
    i2c_master_stop(cmd_handle);
    esp_err_t ret = i2c_master_cmd_begin(SHT30_I2C_PORT, cmd_handle, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Write command failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 等待测量完成 */
    vTaskDelay(pdMS_TO_TICKS(20));

    /* 读取 6 字节：温度(2B) + CRC(1B) + 湿度(2B) + CRC(1B) */
    uint8_t data[6] = {0};
    cmd_handle = i2c_cmd_link_create();
    i2c_master_start(cmd_handle);
    i2c_master_write_byte(cmd_handle, (SHT30_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd_handle, data, 6, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd_handle);
    ret = i2c_master_cmd_begin(SHT30_I2C_PORT, cmd_handle, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Read data failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* CRC 校验 */
    if (sht30_crc8(data, 2) != data[2]) {
        ESP_LOGW(TAG, "Temperature CRC error");
        return ESP_ERR_INVALID_CRC;
    }
    if (sht30_crc8(&data[3], 2) != data[5]) {
        ESP_LOGW(TAG, "Humidity CRC error");
        return ESP_ERR_INVALID_CRC;
    }

    /* 计算温湿度 */
    uint16_t temp_raw = ((uint16_t)data[0] << 8) | data[1];
    uint16_t humi_raw = ((uint16_t)data[3] << 8) | data[4];

    out->temperature = -45.0f + 175.0f * ((float)temp_raw / 65535.0f);
    out->humidity    = 100.0f * ((float)humi_raw / 65535.0f);

    return ESP_OK;
}

esp_err_t sht30_read_status(uint16_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;

    uint8_t cmd[2] = {SHT30_CMD_READ_STATUS >> 8, SHT30_CMD_READ_STATUS & 0xFF};
    i2c_cmd_handle_t cmd_handle = i2c_cmd_link_create();
    i2c_master_start(cmd_handle);
    i2c_master_write_byte(cmd_handle, (SHT30_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd_handle, cmd, 2, true);
    i2c_master_stop(cmd_handle);
    esp_err_t ret = i2c_master_cmd_begin(SHT30_I2C_PORT, cmd_handle, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd_handle);
    if (ret != ESP_OK) return ret;

    vTaskDelay(pdMS_TO_TICKS(5));

    uint8_t data[3] = {0};
    cmd_handle = i2c_cmd_link_create();
    i2c_master_start(cmd_handle);
    i2c_master_write_byte(cmd_handle, (SHT30_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd_handle, data, 3, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd_handle);
    ret = i2c_master_cmd_begin(SHT30_I2C_PORT, cmd_handle, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd_handle);
    if (ret != ESP_OK) return ret;

    if (sht30_crc8(data, 2) != data[2]) {
        return ESP_ERR_INVALID_CRC;
    }

    *status = ((uint16_t)data[0] << 8) | data[1];
    return ESP_OK;
}
