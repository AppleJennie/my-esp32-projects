/**
 * radar_wave_analyzer.h — 雷达呼吸/心率波形深度分析
 *
 * 从 breath_wave_buf / heart_wave_buf 提取：
 *   - 呼吸幅度、呼吸率(波形反推)、周期变异系数 breath_cv
 *   - 连续低幅/无周期时长 breath_pause_sec
 *   - 心率波形幅度、心率(波形反推)、HRV 代理 SDNN
 *   - 波形质量评估 + 体动伪影标记
 *
 * 5Hz 采样, 60 秒窗口 (300 点)
 * 免责声明：hr_sdnn_proxy 仅为雷达心率变异性趋势，非医学级 ECG SDNN
 */
#ifndef RADAR_WAVE_ANALYZER_H
#define RADAR_WAVE_ANALYZER_H

#include <stdint.h>
#include <stdbool.h>
#include "sleep_radar_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 波形参数 ── */
#define WAVE_FS                    5       /* 采样率 Hz */
#define WAVE_BUF_SEC               60      /* 窗口时长 秒 */
#define WAVE_BUF_LEN               (WAVE_FS * WAVE_BUF_SEC)  /* 300 */

/* 呼吸周期约束 */
#define BREATH_PERIOD_MIN_SEC      1.8f    /* 最短呼吸周期 (33 次/min) */
#define BREATH_PERIOD_MAX_SEC      8.0f    /* 最长呼吸周期 (7.5 次/min) */
#define BREATH_CV_STABLE           0.20f   /* < 0.20 稳定 */
#define BREATH_CV_MILD             0.35f   /* 0.20~0.35 轻度不稳 */
#define BREATH_CV_SEVERE           0.50f   /* > 0.50 严重不稳 */

/* 心率间期约束 */
#define HEART_IBI_MIN_SEC          0.45f   /* 最短心跳间隔 (133 bpm) */
#define HEART_IBI_MAX_SEC          1.50f   /* 最长心跳间隔 (40 bpm)  */

/* 暂停检测 */
#define BREATH_PAUSE_DROP_RATIO    0.25f   /* 幅度低于基线 25% */
#define BREATH_PAUSE_MIN_SEC       10      /* 至少持续 10 秒 */

/* 质量门控 */
#define WAVE_QUALITY_MIN           0.50f   /* 低于此值不参与评分 */
#define MOTION_ARTIFACT_THRESH     35      /* 体动超过此值标记伪影 */

/* ═══════════════════════════════════════════════════════════════
 * 输出特征
 * ═══════════════════════════════════════════════════════════════ */

typedef struct {
    /* ── 呼吸 ── */
    float breath_amp;            /* 峰峰值幅度 */
    float breath_rate_wave;      /* 从波形周期反推的呼吸率 (次/min) */
    float breath_cv;             /* 呼吸周期变异系数 */
    float breath_pause_sec;      /* 当前连续低幅/无周期时长 (秒) */
    float breath_quality;        /* 0~1 波形质量 */

    /* ── 心率 ── */
    float heart_amp;             /* 心率波形幅度 */
    float heart_rate_wave;       /* 从波形反推的心率 (bpm) */
    float hr_sdnn_ms;            /* 雷达 HRV 代理 SDNN (ms)，非医学级 */
    float hrv_quality;           /* 0~1 心率波形质量 */

    /* ── 标记 ── */
    bool  motion_artifact;       /* 当前存在体动伪影 */
    bool  valid;                 /* 特征是否可信 */
    bool  breath_cycle_valid;    /* 呼吸周期分析是否有效 */
    bool  heart_cycle_valid;     /* 心率周期分析是否有效 */

    /* ── 元数据 ── */
    int   breath_cycle_count;    /* 检测到的呼吸周期数 */
    int   heart_beat_count;      /* 检测到的心跳数 */
    float breath_amp_baseline;   /* 个人基线幅度 */
} RadarWaveFeatures;

/* ═══════════════════════════════════════════════════════════════
 * API
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief 初始化分析器（清零历史缓冲）
 */
void radar_wave_analyzer_init(void);

/**
 * @brief 从雷达快照中提取波形特征
 * @param radar   雷达数据快照（含波形 ring buffer）
 * @param out     输出特征
 * @param now_ms  当前时间戳
 * @param bl_amp  个人基线呼吸幅度（可为 0，内部用近期均值兜底）
 */
void radar_wave_analyze(const sleep_radar_data_t *radar,
                         RadarWaveFeatures *out,
                         uint32_t now_ms,
                         float bl_amp);

/**
 * @brief 轻量版：仅分析呼吸波形（不需要心率波形）
 */
void radar_wave_analyze_breath_only(const sleep_radar_data_t *radar,
                                     RadarWaveFeatures *out,
                                     uint32_t now_ms,
                                     float bl_amp);

#ifdef __cplusplus
}
#endif

#endif
