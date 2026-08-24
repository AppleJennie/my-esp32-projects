/**
 * sleep_baseline.h — 个人基线采集
 *
 * 条件：presence=true, in_bed=true, breath_valid=true,
 *       heart_valid=true, body_motion <= 15
 *
 * 累计稳定样本 >= 120 秒后 baseline_valid = true
 * 单秒不稳定只跳过，连续 30 秒不稳定才重置
 */
#ifndef SLEEP_BASELINE_H
#define SLEEP_BASELINE_H
#include <stdint.h>
#include <stdbool.h>
#include "fusion_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float  breath_amp;       /* 呼吸波形峰峰值均值 */
    float  breath_bpm;       /* 呼吸率均值 */
    float  heart_bpm;        /* 心率均值 */
    float  body_motion;      /* 体动均值 */
    float  audio_energy;     /* 音频 RMS 能量均值 */
    float  noise_floor;      /* 噪声基底 */
    bool   valid;            /* 基线是否就绪 */
    float  quality;          /* 0~1 */
    uint32_t collected_ms;   /* 收集耗时 */
    int    sample_count;
    int    progress_seconds; /* 0~120, 用于 UI 显示进度 */
    int    unstable_streak;  /* 连续不稳定秒数 */
} baseline_t;

void sleep_baseline_init(void);
void sleep_baseline_reset(void);

/** 每秒调用一次：传入当前雷达和音频帧（音频可为 NULL） */
void sleep_baseline_feed(const radar_feature_t *r, const audio_feature_t *a);

bool sleep_baseline_is_ready(void);
const baseline_t *sleep_baseline_get(void);
int  sleep_baseline_progress_sec(void);

#ifdef __cplusplus
}
#endif
#endif
