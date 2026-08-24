/**
 * snore_event_detector.h — 鼾声事件层
 *
 * 将连续 is_snoring 帧合并为 episode，输出短时统计。
 */
#ifndef SNORE_EVENT_DETECTOR_H
#define SNORE_EVENT_DETECTOR_H

#include <stdint.h>
#include <stdbool.h>
#include "fusion_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 单个鼾声 episode */
typedef struct {
    uint32_t start_ms;
    uint32_t end_ms;
    uint32_t duration_ms;
    uint8_t  max_snore_score;
    uint8_t  avg_snore_score;
    float    avg_rms;
    float    avg_zcr;
    float    avg_low_freq;
    float    avg_harmonic;
} snore_episode_t;

/* 短时统计 */
typedef struct {
    uint32_t last_minute_count;       /* 最近 1 分钟鼾声次数 */
    uint32_t last_minute_duration_ms; /* 最近 1 分钟鼾声总时长 */
    uint32_t last_10min_duration_ms;  /* 最近 10 分钟鼾声总时长 */
    float    snore_burden_pct;        /* 最近 10 分钟鼾声占比 % */
    uint32_t longest_episode_ms;      /* 最长连续鼾声 */
    uint32_t episode_count_total;     /* 累计 episode 数 */
} snore_stats_t;

/* 当前 episode 状态 */
typedef struct {
    bool     active;
    uint32_t start_ms;
    uint32_t last_update_ms;
    uint32_t total_snore_ms;          /* 累计 is_snoring=true 的时长 */
} snore_event_state_t;

/** 初始化 */
void snore_event_detector_init(void);

/** 每帧（每 2-4 秒）调用一次，传入 audio_feature_t */
void snore_event_detector_feed(const audio_feature_t *af, uint32_t now_ms);

/** 获取当前 episode */
const snore_episode_t *snore_event_current_episode(void);
bool snore_event_is_active(void);
uint32_t snore_event_active_duration_ms(uint32_t now_ms);

/** 获取统计 */
void snore_event_get_stats(snore_stats_t *out);

#ifdef __cplusplus
}
#endif
#endif
