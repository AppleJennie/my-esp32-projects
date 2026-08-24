/**
 * sleep_fusion.h — 音频+雷达融合风险筛查引擎
 *
 * 每秒 tick，读取音频特征和雷达特征，输出 fusion_result_t。
 */
#ifndef SLEEP_FUSION_H
#define SLEEP_FUSION_H
#include "fusion_types.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int  sleep_fusion_init(fusion_callback_t cb, void *user);
void sleep_fusion_feed_audio(const audio_feature_t *a);
void sleep_fusion_feed_radar(const radar_feature_t *r);

/** 每秒调用一次（主 tick） */
void sleep_fusion_tick(void);

void sleep_fusion_deinit(void);

/** 获取最新融合结果快照 */
bool sleep_fusion_get_result(fusion_result_t *out);

/** 事件类型 → 字符串 */
const char *sleep_fusion_event_label(int event);

/** 获取累计统计 */
void sleep_fusion_get_stats(uint16_t *apnea_cnt, uint16_t *hypopnea_cnt,
                            uint16_t *recovery_cnt, uint16_t *arousal_cnt);

#ifdef __cplusplus
}
#endif
#endif
