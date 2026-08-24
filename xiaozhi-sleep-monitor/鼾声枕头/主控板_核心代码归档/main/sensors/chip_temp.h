/**
 * ESP32-S3 内置温度传感器驱动（零依赖，零引脚）
 *
 * 原驱动来源: D:\embed32\s3example\23_rgb\basic_routines\18_internal_Temperature\components\BSP\SENSOR\
 *
 * 注意：内置传感器受 CPU 发热影响，误差约 ±5°C，仅作辅助参考。
 *       精确室温请用外部 SHT30。
 */
#ifndef SENSORS_CHIP_TEMP_H
#define SENSORS_CHIP_TEMP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化 ESP32-S3 内部温度传感器
 * @return 0 成功，非 0 失败
 */
int chip_temp_init(void);

/**
 * 读取芯片温度（摄氏度）
 * @return 温度值（整型，如需浮点可改用 chip_temp_read_float）
 */
short chip_temp_read(void);

/**
 * 读取芯片温度（浮点，摄氏度）
 */
float chip_temp_read_float(void);

#ifdef __cplusplus
}
#endif

#endif /* SENSORS_CHIP_TEMP_H */
