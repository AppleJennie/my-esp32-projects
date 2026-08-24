/**
 * main_report_uart.h — 小智主板 → UI 板 MAIN 数据发送
 *
 * 每秒通过 UART2 TX (GPIO20) 发送一行 MAIN 文本给 UI 板。
 * 与 snore_report_rx 共用同一 UART2（RX=GPIO19 收 SNORE，TX=GPIO20 发 MAIN）。
 *
 * 协议格式（25 个字段）：
 *   MAIN,ts_ms,radar_status,heart_valid,heart_bpm,breath_valid,breath_bpm,
 *   motion_valid,body_motion,bed_valid,in_bed,stage_valid,sleep_stage,
 *   risk_valid,risk_level,event_id,apnea_count,hypopnea_count,
 *   system_status,wifi_ok,sd_ok,spo2_valid,spo2,temp_valid,temp_x10,hum_x10
 */
#ifndef MAIN_REPORT_UART_H
#define MAIN_REPORT_UART_H

#include <stdint.h>
#include <stdbool.h>
#include "app_role_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化 MAIN TX（UART2 已在 snore_report_rx_start 中初始化，此处仅打印日志）
 */
bool main_report_uart_init(void);

/**
 * 读取最新融合数据，格式化为 MAIN 文本，通过 UART2 TX 发送。
 * 即使雷达无效也发送（用于 UI 板判断主板在线）。
 */
void main_report_uart_send(void);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_REPORT_UART_H */
