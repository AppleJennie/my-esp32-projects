/**
 * fusion_task.h — 小智主板融合任务
 *
 * 每秒 tick：
 *   1. 从 snore_report_rx 获取最新 SNORE 文本
 *   2. 转为 audio_feature_t → sleep_fusion_feed_audio()
 *   3. 从 sleep_radar_data 获取最新雷达数据
 *   4. 转为 radar_feature_t → sleep_fusion_feed_radar()
 *   5. 从板载传感器（SHT30/AP3216C）读取环境数据
 *   6. sleep_fusion_tick() + sleep_health_fusion_tick()
 *   7. 融合结果写入 SleepDataCenter → MCP 工具可查询
 */
#ifndef FUSION_TASK_H
#define FUSION_TASK_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化融合引擎及所有子模块：
 *   - sleep_radar_data
 *   - sleep_fusion
 *   - sleep_health_fusion
 *   - sleep_baseline
 *   - radar_breath_event
 *   - snore_event_detector
 * @return true 成功
 */
bool fusion_task_init(void);

/**
 * 启动 R60 UART 接收 + 融合定时任务
 * @return true 成功
 */
bool fusion_task_start(void);

/**
 * 停止融合任务
 */
void fusion_task_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* FUSION_TASK_H */
