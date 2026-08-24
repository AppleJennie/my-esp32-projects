/**
 * AP3216C 驱动实现（ESP-IDF v5.4 原生 I2C API，适配 ATK DNESP32S3）
 */
#include "ap3216c.h"
#include <esp_log.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"

static const char *TAG = "AP3216C";

/* ---- 底层 I2C 读写（替代原 BSP 的 i2c_transfer） ---- */

static esp_err_t ap3216c_write_reg(uint8_t reg, uint8_t data)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (AP3216C_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(AP3216C_I2C_PORT, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t ap3216c_read_reg(uint8_t reg, uint8_t *data)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (AP3216C_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (AP3216C_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, data, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(AP3216C_I2C_PORT, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return ret;
}

/* ---- 公开 API ---- */

esp_err_t ap3216c_init(void)
{
    /* 复位 */
    esp_err_t ret = ap3216c_write_reg(0x00, 0x04);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Reset write failed: %s", esp_err_to_name(ret));
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(50));  /* 手册要求复位后至少等 10ms */

    /* 开启 ALS + PS + IR */
    ret = ap3216c_write_reg(0x00, 0x03);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Enable write failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 验证 */
    uint8_t verify = 0;
    ret = ap3216c_read_reg(0x00, &verify);
    if (ret == ESP_OK && verify == 0x03) {
        ESP_LOGI(TAG, "AP3216C init OK, I2C0 addr=0x%02X", AP3216C_I2C_ADDR);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "AP3216C verify failed (read 0x%02X, expected 0x03), ret=%s",
             verify, esp_err_to_name(ret));
    return ESP_ERR_NOT_FOUND;
}

esp_err_t ap3216c_read(ap3216c_data_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;

    uint8_t buf[6] = {0};

    /* 连续读 6 个寄存器（0x0A ~ 0x0F） */
    for (int i = 0; i < 6; i++) {
        esp_err_t ret = ap3216c_read_reg(0x0A + i, &buf[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Read reg 0x%02X failed: %s", 0x0A + i, esp_err_to_name(ret));
            return ret;
        }
    }

    /* IR */
    if (buf[0] & 0x80) {
        out->ir = 0;
    } else {
        out->ir = ((uint16_t)buf[1] << 2) | (buf[0] & 0x03);
    }

    /* ALS */
    out->als = ((uint16_t)buf[3] << 8) | buf[2];

    /* PS */
    if (buf[4] & 0x40) {
        out->ps = 0;
    } else {
        out->ps = ((uint16_t)(buf[5] & 0x3F) << 4) | (buf[4] & 0x0F);
    }

    return ESP_OK;
}
