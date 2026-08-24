/**
 * AP3216C 环境光 + 接近感应传感器驱动（适配 ESP-IDF v5.4 + ATK I2C0 共享总线）
 *
 * 原驱动来源: D:\embed32\s3example\23_rgb\components\BSP\AP3216C\
 *
 * 适配说明：
 *   - 将正点原子 BSP 的 i2c_obj_t / i2c_transfer 封装替换为 ESP-IDF 原生 I2C 事务 API
 *   - I2C0 总线已被 Board 层初始化，此处不重复初始化
 */
#ifndef SENSORS_AP3216C_H
#define SENSORS_AP3216C_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* AP3216C I2C 地址 */
#define AP3216C_I2C_ADDR    0x1E

/* I2C 端口（与音频 ES8388、XL9555、SHT30 共用 I2C0） */
#define AP3216C_I2C_PORT    I2C_NUM_0

typedef struct {
    uint16_t ir;    /* 红外值 */
    uint16_t als;   /* 环境光照值 */
    uint16_t ps;    /* 接近感应值 */
} ap3216c_data_t;

/**
 * 初始化 AP3216C（I2C0 已由 Board 层配置）
 * 发送复位 → 开启 ALS+PS+IR → 验证
 * @return ESP_OK 成功
 */
esp_err_t ap3216c_init(void);

/**
 * 读取传感器原始数据
 * 注意：连续两次读取需间隔 >= 112.5ms
 */
esp_err_t ap3216c_read(ap3216c_data_t *out);

#ifdef __cplusplus
}
#endif

#endif /* SENSORS_AP3216C_H */
