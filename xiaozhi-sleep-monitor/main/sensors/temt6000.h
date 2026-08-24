/**
 * TEMT6000 环境光传感器驱动（ADC 模拟输出）
 *
 * 接线: VCC→3.3V, GND→GND, SIG→GPIO4 (ADC1_CH3)
 * 引脚定义在 app_role_config.h
 */
#ifndef SENSORS_TEMT6000_H
#define SENSORS_TEMT6000_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化 ADC1_CH3 (GPIO4)
 * @return ESP_OK 成功
 */
esp_err_t temt6000_init(void);

/**
 * 读取光照原始 ADC 值（0~4095）
 * @return ADC raw 值, -1 表示未初始化
 */
int temt6000_read_raw(void);

/**
 * 读取估算照度（lux）
 * 公式: lux ≈ raw * 3.3 / 4095 * 1000 / 10kΩ 粗略估算
 * @return 照度 lux, -1 表示错误
 */
float temt6000_read_lux(void);

#ifdef __cplusplus
}
#endif

#endif
