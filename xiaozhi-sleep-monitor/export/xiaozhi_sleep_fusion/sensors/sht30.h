/**
 * SHT30 数字温湿度传感器驱动（适配 ESP-IDF v5.4 新版 I2C 总线）
 *
 * 原驱动来源: D:\embed32\s3example\01_led\components\BSP\SHT30\
 *
 * 适配说明：
 *   - ATK DNESP32S3 的 I2C0 已由 Board 层通过 i2c_new_master_bus() 初始化
 *   - SHT30 共用 I2C0（GPIO41=SDA, GPIO42=SCL），与 ES8388/XL9555/AP3216C 共用
 *   - 不再自行调用 i2c_param_config / i2c_driver_install，避免冲突
 *   - I2C 事务层 API（i2c_cmd_link_create / i2c_master_cmd_begin）与新旧驱动均兼容
 */
#ifndef SENSORS_SHT30_H
#define SENSORS_SHT30_H

#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "driver/i2c.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SHT30 I2C 地址（ADDR 引脚接 GND = 0x44，接 VCC = 0x45） */
#define SHT30_I2C_ADDR          0x44

/* 常用命令 */
#define SHT30_CMD_MEAS_HIGHREP_STRETCH  0x2C06
#define SHT30_CMD_MEAS_MEDREP_STRETCH   0x2C0D
#define SHT30_CMD_MEAS_LOWREP_STRETCH   0x2C10
#define SHT30_CMD_SOFT_RESET            0x30A2
#define SHT30_CMD_READ_STATUS           0xF32D

/* ATK 板 I2C0 总线 */
#define SHT30_I2C_PORT          I2C_NUM_0

typedef struct {
    float temperature;   /* 摄氏度 */
    float humidity;      /* %RH */
} sht30_data_t;

/**
 * 初始化 SHT30（I2C0 总线已被 Board 层初始化）
 * 此函数发送软复位并验证传感器是否存在
 * @return ESP_OK 成功，否则失败
 */
esp_err_t sht30_init(void);

/**
 * 软复位 SHT30
 */
esp_err_t sht30_soft_reset(void);

/**
 * 单次测量（阻塞约 25ms）
 * @param out 输出温湿度数据
 * @return ESP_OK 成功
 */
esp_err_t sht30_read(sht30_data_t *out);

/**
 * 读取状态寄存器
 */
esp_err_t sht30_read_status(uint16_t *status);

#ifdef __cplusplus
}
#endif

#endif /* SENSORS_SHT30_H */
