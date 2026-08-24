/**
 * app_role_config.h — 小智主板角色配置
 *
 * 主板职责：
 *   1. R60ABD1 毫米波雷达采集（UART1）
 *   2. 接收 UI 板 SNORE 文本上报（UART2）
 *   3. 音频+雷达融合风险筛查
 *   4. 健康风险分析 + MCP 语音播报 + 云端联网
 *
 * 主板不做：
 *   - 本地 INMP441 I2S 音频采集
 *   - 本地 TFLM 鼾声模型推理
 *   - LVGL 睡眠大屏 UI
 */
#ifndef APP_ROLE_CONFIG_H
#define APP_ROLE_CONFIG_H

/* 角色标识 */
#define APP_ROLE_XIAOZHI_MAIN              1

/* ── 主板启用的功能 ── */
#define CONFIG_ENABLE_R60_RADAR            1   /* R60ABD1 毫米波雷达 */
#define CONFIG_ENABLE_SNORE_REPORT_RX      1   /* 接收 UI 板 SNORE 文本 */
#define CONFIG_ENABLE_SLEEP_FUSION         1   /* 音频+雷达融合引擎 */
#define CONFIG_ENABLE_HEALTH_FUSION        1   /* 健康风险分析 */
#define CONFIG_ENABLE_ONBOARD_SENSORS      0   /* 板载传感器（禁用，待新 I2C API 适配） */
#define CONFIG_ENABLE_SHT30                0
#define CONFIG_ENABLE_AP3216C              0
#define CONFIG_ENABLE_CHIP_TEMP            0
#define CONFIG_ENABLE_ENV_SENSOR           0

/* ── 主板禁用的功能 ── */
#define CONFIG_ENABLE_LOCAL_INMP441        0   /* 不采集本地 I2S 麦克风 */
#define CONFIG_ENABLE_LOCAL_SNORE_MODEL    0   /* 不跑本地 TFLM 鼾声模型 */
#define CONFIG_ENABLE_LVGL_SLEEP_UI        0   /* 不启动 LVGL 睡眠大屏 UI */

/* ── UART 通道分配 ── */
#define UART_R60_NUM       UART_NUM_1       /* R60 雷达: GPIO17(RX) GPIO18(TX) */

/* 小智 ↔ UI 双向通讯: UART2 */
#define BOARD_LINK_UART      UART_NUM_2     /* 共用 UART2 */
#define BOARD_LINK_RX_GPIO   GPIO_NUM_19    /* 收 UI SNORE */
#define BOARD_LINK_TX_GPIO   GPIO_NUM_20    /* 发 MAIN 给 UI */
#define BOARD_LINK_BAUD      115200

/* 兼容旧宏名 */
#define UART_SNORE_RX_NUM   BOARD_LINK_UART
#define UART_SNORE_RX_TX    BOARD_LINK_TX_GPIO
#define UART_SNORE_RX_RX    BOARD_LINK_RX_GPIO
#define UART_SNORE_RX_BAUD  BOARD_LINK_BAUD

#define MAIN_REPORT_UART_PORT  BOARD_LINK_UART
#define MAIN_REPORT_RX_GPIO    BOARD_LINK_RX_GPIO
#define MAIN_REPORT_TX_GPIO    BOARD_LINK_TX_GPIO
#define MAIN_REPORT_BAUD       BOARD_LINK_BAUD

#endif /* APP_ROLE_CONFIG_H */
