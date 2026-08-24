#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ================================================================
 * 硬件功能开关
 * ================================================================ */
#define CONFIG_ENABLE_RGB_LCD        1
#define CONFIG_ENABLE_TOUCH          1
#define CONFIG_ENABLE_ES8388_AUDIO   0
#define CONFIG_ENABLE_INMP441_AUDIO  1
#define CONFIG_ENABLE_SD_LOGGER      0
#define CONFIG_ENABLE_FLASH_LOGGER   1
#define CONFIG_ENABLE_R60_RADAR      0   /* 第一阶段关闭，后续 UART2 GPIO20/19 */
#define CONFIG_ENABLE_I2C_SENSORS    0   /* 第一阶段关闭，后续 I2C0 GPIO41/42 */

/* ================================================================
 * 日志模式
 * ================================================================ */
#define LOG_MODE_DEBUG        0  /* 全量日志 */
#define LOG_MODE_SNORE_ONLY   1  /* 仅鼾声分析 */
#define LOG_MODE_FUSION_ONLY  2  /* 仅融合结果 */

#define APP_LOG_MODE LOG_MODE_SNORE_ONLY

/* ================================================================
 * 雷达日志抑制
 * ================================================================ */
#define R60_LOG_RAW_ENABLE       0  /* 原始帧 HEX 打印 */
#define R60_LOG_WAVE_ENABLE      0  /* 波形 len 打印 */
#define R60_LOG_PERIODIC_ENABLE  0  /* 体动/距离/坐标每帧打印 */

/* ================================================================
 * UI 串口日志
 * ================================================================ */
#define UI_SERIAL_LOG_ENABLE     0  /* [UI] 状态行打印 */

/* ================================================================
 * 特征宏
 * ================================================================ */
#define CONFIG_ENABLE_AUDIO      1
#define CONFIG_ENABLE_TFLITE     1

void app_config_set_log_levels(void);

#endif
