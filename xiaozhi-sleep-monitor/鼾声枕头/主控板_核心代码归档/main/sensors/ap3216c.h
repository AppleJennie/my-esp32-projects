/**
 * AP3216C 环境光传感器驱动（ESP-IDF v5.5 新版 I2C API）
 *
 * 挂载在 ATK 主板 I2C0 总线上（GPIO41/42），与 ES8388/XL9555 共用。
 * 通过 board_get_i2c0_handle() 获取总线句柄。
 */
#ifndef SENSORS_AP3216C_H
#define SENSORS_AP3216C_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AP3216C_I2C_ADDR    0x1E

typedef struct {
    uint16_t ir;   /* 红外 */
    uint16_t als;  /* 环境光 lux */
    uint16_t ps;   /* 接近感应 */
} ap3216c_data_t;

/**
 * 初始化 AP3216C（使用 ATK 板 I2C0 总线）
 * @return ESP_OK 成功
 */
esp_err_t ap3216c_init(void);

/**
 * 读取传感器数据
 */
esp_err_t ap3216c_read(ap3216c_data_t *out);

#ifdef __cplusplus
}
#endif

#endif
