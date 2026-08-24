/**
 * SHT30 数字温湿度传感器驱动（独立 I2C1 总线, GPIO8/11）
 *
 * 原驱动来源: D:\embed32\s3example\01_led\components\BSP\SHT30\
 *
 * 适配说明：
 *   - 使用独立 I2C_NUM_1 (GPIO8=SDA, GPIO11=SCL)
 *   - 与 ATK 主板的 I2C0（GPIO41/42 ES8388 音频）完全隔离，无新旧 API 冲突
 *   - 引脚定义在 app_role_config.h
 */
#ifndef SENSORS_SHT30_H
#define SENSORS_SHT30_H

#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SHT30 I2C 地址 */
#define SHT30_I2C_ADDR          0x44

/* 常用命令 */
#define SHT30_CMD_MEAS_HIGHREP_STRETCH  0x2C06
#define SHT30_CMD_SOFT_RESET            0x30A2
#define SHT30_CMD_READ_STATUS           0xF32D

typedef struct {
    float temperature;
    float humidity;
} sht30_data_t;

esp_err_t sht30_init(void);
esp_err_t sht30_soft_reset(void);
esp_err_t sht30_read(sht30_data_t *out);
esp_err_t sht30_read_status(uint16_t *status);

#ifdef __cplusplus
}
#endif

#endif
