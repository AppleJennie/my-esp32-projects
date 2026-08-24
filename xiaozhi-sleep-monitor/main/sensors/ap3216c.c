/**
 * AP3216C 驱动实现（新版 i2c_master API）
 *
 * 使用 i2c_master_bus_add_device + i2c_master_transmit/receive
 */
#include "ap3216c.h"
#include <driver/i2c_master.h>
#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "AP3216C";

static i2c_master_dev_handle_t s_dev = NULL;
static bool s_ready = false;

/* 获取 ATK 板的 I2C0 bus handle */
extern i2c_master_bus_handle_t board_get_i2c0_handle(void);

esp_err_t ap3216c_init(void)
{
    i2c_master_bus_handle_t bus = board_get_i2c0_handle();
    if (!bus) {
        ESP_LOGE(TAG, "I2C0 bus not ready");
        return ESP_ERR_INVALID_STATE;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AP3216C_I2C_ADDR,
        .scl_speed_hz = 400000,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bus add device failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 复位 */
    uint8_t reset_cmd[] = {0x00, 0x04};
    ret = i2c_master_transmit(s_dev, reset_cmd, 2, 50);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(50));

    /* 开启 ALS+PS+IR */
    uint8_t enable_cmd[] = {0x00, 0x03};
    ret = i2c_master_transmit(s_dev, enable_cmd, 2, 50);
    if (ret != ESP_OK) return ret;

    /* 验证 */
    uint8_t reg = 0x00, verify = 0;
    ret = i2c_master_transmit_receive(s_dev, &reg, 1, &verify, 1, 50);
    if (ret == ESP_OK && verify == 0x03) {
        s_ready = true;
        ESP_LOGI(TAG, "Init OK I2C0 addr=0x%02X", AP3216C_I2C_ADDR);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Verify failed: read 0x%02X expected 0x03", verify);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t ap3216c_read(ap3216c_data_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!s_ready) {
        memset(out, 0, sizeof(*out));
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[6] = {0};
    for (int i = 0; i < 6; i++) {
        uint8_t reg = 0x0A + i;
        esp_err_t ret = i2c_master_transmit_receive(s_dev, &reg, 1, &data[i], 1, 50);
        if (ret != ESP_OK) return ret;
    }

    out->ir  = (data[0] & 0x80) ? 0 : ((uint16_t)data[1] << 2) | (data[0] & 0x03);
    out->als = ((uint16_t)data[3] << 8) | data[2];
    out->ps  = (data[4] & 0x40) ? 0 : ((uint16_t)(data[5] & 0x3F) << 4) | (data[4] & 0x0F);
    return ESP_OK;
}
