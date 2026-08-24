#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化睡眠监测模块（加载 TFLM 模型、特征提取器，不启动音频）
 * @return true 成功
 */
bool sleep_monitor_init(void);

/**
 * @brief 启动睡眠监测（初始化麦克风，启动采集和推理任务）
 * @return true 成功
 */
bool sleep_monitor_start(void);

/**
 * @brief 停止睡眠监测（停止任务，释放麦克风）
 */
void sleep_monitor_stop(void);

/**
 * @brief 是否正在监测
 */
bool sleep_monitor_is_running(void);

/**
 * @brief 获取累计鼾声事件数
 */
int sleep_monitor_get_snore_count(void);

/**
 * @brief 获取当前鼾声概率 (0.0 ~ 1.0)
 */
float sleep_monitor_get_snore_probability(void);

/**
 * @brief 获取睡眠质量评分 (0-100，仅基于鼾声频率简化估算)
 */
float sleep_monitor_get_quality_score(void);

/**
 * @brief 获取监测报告文本
 * @param buf 输出缓冲区
 * @param len 缓冲区长度
 */
void sleep_monitor_get_report(char* buf, size_t len);

#ifdef __cplusplus
}
#endif
