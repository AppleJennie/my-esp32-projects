/**
 * radar_breath_event.h — 雷达呼吸事件检测
 *
 * 基于 radar_feature_t 判断呼吸稳定/变浅/不规则/疑似暂停。
 * 不输出医学诊断，仅作为健康风险筛查的参考输入。
 */
#ifndef RADAR_BREATH_EVENT_H
#define RADAR_BREATH_EVENT_H

#include <stdint.h>
#include <stdbool.h>
#include "fusion_types.h"
#include "sleep_baseline.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BREATH_EVT_STABLE              = 0,
    BREATH_EVT_SHALLOW             = 1,  /* 疑似呼吸变浅 */
    BREATH_EVT_IRREGULAR           = 2,  /* 疑似呼吸不规则 */
    BREATH_EVT_PAUSE_SUSPECTED     = 3,  /* 疑似短时呼吸暂停 */
    BREATH_EVT_RECOVERY            = 4,  /* 疑似恢复性大呼吸 */
    BREATH_EVT_MOVEMENT_ARTIFACT   = 5,  /* 体动干扰 */
    BREATH_EVT_DATA_INVALID        = 6,  /* 数据无效 */
    BREATH_EVT_UNKNOWN             = 7,
} breath_event_type_t;

typedef struct {
    breath_event_type_t type;
    float    breath_amp;           /* 当前幅度 */
    float    breath_amp_baseline;  /* 基线幅度 */
    float    drop_ratio;           /* 下降比例 */
    float    quality;
    uint8_t  confidence;           /* 0~100 */
    char     reason[80];
} breath_event_t;

/** 初始化 */
void radar_breath_event_init(void);

/** 每秒调用一次 */
void radar_breath_event_feed(const radar_feature_t *rf, const baseline_t *bl, uint32_t now_ms);

/** 获取当前事件 */
const breath_event_t *radar_breath_event_get(void);

#ifdef __cplusplus
}
#endif
#endif
