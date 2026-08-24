/**
 * @file    sleep_project_config.h
 * @brief   睡眠监测项目硬件功能开关
 *
 * 第一阶段：仅 INMP441 音频采集 + 鼾声AI
 * GPIO11/12/13 被 INMP441 I2S1 占用，关闭 SPI SD 卡
 */

#ifndef SLEEP_PROJECT_CONFIG_H
#define SLEEP_PROJECT_CONFIG_H

/* ================================================================
 * 屏幕仅演示模式 (无需硬件传感器)
 * ================================================================ */
#define CONFIG_SCREEN_ONLY             0   /* 1=仅屏幕演示(无传感器) 0=正常模式 */

/* ================================================================
 * 第一阶段: 仅音频 + 鼾声AI (audio-only mode)
 * ================================================================ */
#define CONFIG_ENABLE_INMP441_AUDIO     1
#define CONFIG_ENABLE_SNORE_AI          1

#define CONFIG_ENABLE_ES8388_AUDIO      0
#define CONFIG_ENABLE_SD_CARD           0
#define CONFIG_ENABLE_MUSIC_PLAYER      0
#define CONFIG_ENABLE_WIFI              0
#define CONFIG_ENABLE_WEATHER           0
#define CONFIG_ENABLE_R60_RADAR         0  /* 雷达交给对端主板 */
#define CONFIG_ENABLE_SLEEP_FUSION       0  /* 融合交给对端主板 */
#define CONFIG_ENABLE_HEALTH_FUSION      0  /* 健康风险交给对端主板 */
#define CONFIG_ENABLE_BASELINE           0  /* 基线交给对端主板 */
#define CONFIG_ENABLE_I2C_SENSORS       0
#define CONFIG_ENABLE_AUDIO_ONLY_MODE   1

#endif /* SLEEP_PROJECT_CONFIG_H */
