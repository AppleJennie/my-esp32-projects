/**
 * sleep_health_fusion.h — 音频鼾声 + 雷达呼吸 融合健康分析
 *
 * 所有输出均为"疑似风险筛查"，不是医学诊断。
 */
#ifndef SLEEP_HEALTH_FUSION_H
#define SLEEP_HEALTH_FUSION_H

#include <stdint.h>
#include <stdbool.h>
#include "fusion_types.h"
#include "snore_event_detector.h"
#include "radar_breath_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 鼾声类型倾向 ── */
typedef enum {
    SNORE_CLASS_NONE                = 0,
    SNORE_CLASS_SIMPLE              = 1,  /* 普通鼾声 */
    SNORE_CLASS_MOUTH_BREATH_LIKE   = 2,  /* 疑似口呼吸/气流声 */
    SNORE_CLASS_THROAT_SNORE_LIKE   = 3,  /* 疑似喉鼾 */
    SNORE_CLASS_OBSTRUCTIVE_RISK    = 4,  /* 疑似阻塞倾向 */
    SNORE_CLASS_MOVEMENT_ARTIFACT   = 5,  /* 体动伪影 */
    SNORE_CLASS_MIXED               = 6,  /* 混合型 */
} snore_class_t;

/* ── 健康风险等级 ── */
typedef enum {
    HLTH_RISK_NORMAL                 = 0,  /* 正常 */
    HLTH_RISK_LIGHT_SNORE            = 1,  /* 轻度鼾声 */
    HLTH_RISK_BREATH_RESTRICTION     = 2,  /* 疑似呼吸受限风险 */
    HLTH_RISK_HYPOPNEA_SUSPECTED     = 3,  /* 疑似低通气风险 */
    HLTH_RISK_APNEA_SUSPECTED        = 4,  /* 疑似呼吸暂停风险 */
    HLTH_RISK_DATA_LOW_QUALITY       = 5,  /* 数据质量不足 */
} health_risk_t;

/* ── 融合结果 ── */
typedef struct {
    uint32_t       timestamp_ms;
    snore_class_t  snore_class;
    health_risk_t  health_risk;
    uint8_t        confidence;       /* 0~100 */
    char           event_name[48];
    char           suggestion[160];

    /* 音频 */
    bool     is_snoring;
    uint8_t  snore_score;
    float    snore_prob;
    float    rms;
    float    peak;

    /* 雷达 */
    float    breath_bpm;
    float    heart_bpm;
    uint8_t  body_motion;
    float    breath_amp;
    bool     in_bed;
    bool     presence;

    /* 统计 */
    uint32_t snore_episode_count;
    uint32_t snore_duration_total_ms;
    uint32_t longest_snore_ms;
    float    snore_burden_pct;
    uint32_t shallow_breath_events;
    uint32_t pause_suspected_events;
    uint32_t recovery_events;
} health_fusion_result_t;

/** 初始化 */
void sleep_health_fusion_init(void);

/**
 * 每帧调用（跟随 fusion_and_log_task 的节奏）。
 * @param af    当前音频特征
 * @param rf    当前雷达特征
 * @param bl    基线 (可为 NULL)
 * @param now_ms 当前时间 ms
 */
void sleep_health_fusion_tick(const audio_feature_t *af,
                               const radar_feature_t *rf,
                               const baseline_t *bl,
                               uint32_t now_ms);

/** 获取最新结果 */
const health_fusion_result_t *sleep_health_fusion_get(void);

/** 类型/风险→字符串 */
const char *snore_class_label(snore_class_t c);
const char *health_risk_label(health_risk_t r);

#ifdef __cplusplus
}
#endif
#endif
