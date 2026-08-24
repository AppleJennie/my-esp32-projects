/**
 * snore_report_rx.h — 接收 UI 板 SNORE 文本上报
 *
 * UI 板每秒通过 UART 发送一行 SNORE 文本：
 *   SNORE,ts_ms,mic_ok,audio_valid,snore_active,snore_score,
 *   snore_db,rms,peak,zcr_x100,current_episode_ms,snore_total_ms,
 *   longest_episode_ms,snore_episode_count,quality
 *
 * 示例：
 *   SNORE,123456,1,1,0,85,0,320,1200,6,0,0,0,0,0
 */
#ifndef SNORE_REPORT_RX_H
#define SNORE_REPORT_RX_H

#include <stdint.h>
#include <stdbool.h>
#include "app_role_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SNORE 解析结果 */
typedef struct {
    uint32_t ts_ms;

    uint8_t mic_ok;
    uint8_t audio_valid;
    uint8_t snore_active;
    uint8_t snore_score;

    uint8_t snore_db;
    uint16_t rms;
    uint16_t peak;
    uint8_t zcr_x100;

    uint32_t current_episode_ms;
    uint32_t snore_total_ms;
    uint32_t longest_episode_ms;
    uint16_t snore_episode_count;

    uint8_t  quality;
    uint32_t rx_ms;       /* 接收到的时间戳（主板本地时间） */
    bool     valid;
} snore_report_t;

/**
 * 启动 SNORE 接收任务
 * UART 参数定义在 app_role_config.h
 * @return true 成功
 */
bool snore_report_rx_start(void);

/**
 * 获取最新接收到的 SNORE 报告（线程安全）
 * @param out 输出
 * @return true 有有效数据
 */
bool snore_report_rx_get_latest(snore_report_t *out);

/**
 * 获取 SNORE 数据距现在是否在 timeout_ms 内更新过
 */
bool snore_report_is_fresh(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* SNORE_REPORT_RX_H */
