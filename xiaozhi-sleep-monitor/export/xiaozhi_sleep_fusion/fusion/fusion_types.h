/**
 * fusion_types.h — 统一类型定义：音频特征、雷达特征、融合结果
 *
 * 所有结构体保持 C 兼容，也可被 C++ 引用。
 */
#ifndef FUSION_TYPES_H
#define FUSION_TYPES_H
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

#define FUSION_RING_SEC  120

/* ================================================================
 * 音频特征（每秒一帧）
 * ================================================================ */
typedef struct {
    uint32_t timestamp_ms;
    bool     audio_valid;
    bool     mic_connected;
    bool     feature_valid;
    bool     model_enabled;
    bool     model_valid;
    float    rms_energy;
    int16_t  peak;
    float    zcr;
    float    spectral_centroid;
    float    low_freq_ratio;
    float    harmonic_ratio;
    float    noise_floor;
    bool     noise_too_high;
    float    snore_prob;
    bool     is_snoring;
    int      snore_type;            /* 0=none,1=nasal,2=throat,3=mouth,4=mixed */
    float    snore_type_confidence;
    const char *type_hint;          /* 指向静态字符串 */
    bool     airflow_sound_present;
    bool     recovery_breath_sound;
    uint32_t inference_time_ms;
    uint32_t model_age_ms;          /* 特征生成时间相对于现在的年龄 */
} audio_feature_t;

/* ================================================================
 * 雷达特征（每秒一帧）
 * ================================================================ */
typedef struct {
    uint32_t timestamp_ms;
    bool     radar_connected;
    bool     presence_valid;        uint8_t  presence;
    bool     presence_inferred;
    bool     bed_valid;             uint8_t  in_bed;
    bool     breath_valid;          float    breath_bpm;   uint8_t breath_status;
    bool     heart_valid;           float    heart_bpm;
    bool     motion_valid;          uint8_t  body_motion;  uint8_t activity_state;
    bool     position_valid;        uint16_t distance_cm;  int16_t x_cm, y_cm, z_cm;
    bool     breath_wave_valid;     uint8_t breath_wave[5]; float breath_amp;  float breath_quality;
    bool     heart_wave_valid;      uint8_t heart_wave[5];  float heart_amp;   float heart_quality;
} radar_feature_t;

/* ================================================================
 * 窗口统计特征（10s / 30s）
 * ================================================================ */
typedef struct {
    /* 音频 */
    float snore_ratio_10s;
    float snore_ratio_30s;
    float audio_energy_mean_10s;
    float audio_energy_current;
    float audio_energy_drop_ratio;
    float airflow_present_ratio_10s;
    bool  recovery_breath_detected;
    int   main_snore_type_30s;
    float audio_quality_score;

    /* 雷达 */
    float breath_amp_mean_10s;
    float breath_amp_mean_30s;
    float breath_amp_current;
    float breath_amp_drop_ratio;
    float breath_rate_mean_30s;
    float heart_rate_mean_30s;
    float heart_rate_current;
    float heart_rate_rise_ratio;
    float motion_max_10s;
    float motion_mean_10s;
    float radar_quality_score;
    bool  in_bed_stable;
    bool  presence_stable;
} window_features_t;

/* ================================================================
 * 事件评分
 * ================================================================ */
typedef struct {
    int  audio_score;
    int  radar_score;
    int  duration_score;
    int  recovery_score;
    int  penalty_score;
    int  total_score;
    int  confidence;
    char reason[160];
} event_score_t;

/* ================================================================
 * 融合事件类型 + 融合结果
 * ================================================================ */
typedef enum {
    FUSION_NORMAL                  = 0,
    FUSION_SIMPLE_SNORE             = 1,
    FUSION_SUSPECTED_OBSTRUCTIVE    = 2,  /* 疑似阻塞性呼吸暂停风险 */
    FUSION_SUSPECTED_CENTRAL        = 3,  /* 疑似中枢性呼吸暂停风险 */
    FUSION_SUSPECTED_HYPOPNEA       = 4,  /* 疑似低通气风险 */
    FUSION_RECOVERY_BREATH          = 5,  /* 恢复呼吸 */
    FUSION_MOVEMENT_ARTIFACT        = 6,  /* 体动干扰 */
    FUSION_BODY_MOVEMENT_AROUSAL    = 7,  /* 体动觉醒 */
    FUSION_DATA_QUALITY_LOW         = 8,  /* 数据质量不足 */
    FUSION_WARMING_UP               = 9,  /* 雷达预热中/等待数据 */
} fusion_event_t;

typedef struct {
    uint32_t       timestamp_ms;
    fusion_event_t event;
    uint8_t        severity;           /* 0~3 */
    uint8_t        confidence;         /* 0~100 */
    uint16_t       duration_sec;
    int            total_score;
    float          audio_quality;
    float          radar_quality;
    bool           baseline_valid;
    int            baseline_progress;  /* 0~120s */
    bool           data_valid;

    /* 关键生命体征 */
    float          heart_bpm;
    float          breath_bpm;
    uint8_t        body_motion;
    uint8_t        breath_status;
    bool           presence;
    bool           in_bed;

    /* 鼾声 */
    bool           snoring;
    int            snore_type;
    float          snore_prob;

    /* 统计 */
    uint16_t       suspected_apnea_count;
    uint16_t       suspected_hypopnea_count;
    uint16_t       recovery_breath_count;
    uint16_t       movement_arousal_count;
    uint16_t       snore_sec_total;

    /* 雷达信号 */
    float          breath_amp;
    float          heart_amp;
    uint16_t       distance_cm;

    char           reason[160];
} fusion_result_t;

/* 回调 */
typedef void (*fusion_callback_t)(const fusion_result_t *r, void *user);

/* ================================================================
 * 鼾声类型建议文本（静态字符串，不占结构体）
 * ================================================================ */
static inline const char *snore_type_suggest(int snore_type) {
    switch (snore_type) {
        case 1: return "疑似鼻鼾，可能与鼻腔通气不畅有关，建议关注鼻塞、空气湿度和睡姿。";
        case 2: return "疑似喉鼾，可能存在咽部软组织振动，建议结合雷达呼吸波形观察阻塞风险。";
        case 3: return "疑似口呼吸，建议关注张口睡眠、口干和鼻腔通气情况。";
        case 4: return "混合型鼾声，建议结合呼吸率、体动和夜间事件综合判断。";
        default: return "未检测到明显鼾声。";
    }
}

static inline const char *snore_type_short_name(int snore_type) {
    switch (snore_type) {
        case 1: return "nasal";
        case 2: return "throat";
        case 3: return "mouth";
        case 4: return "mixed";
        default: return "none";
    }
}

#ifdef __cplusplus
}
#endif
#endif
