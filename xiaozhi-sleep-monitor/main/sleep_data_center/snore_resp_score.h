#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 呼吸事件类型（纯雷达判定，无音频）
 * ================================================================ */
typedef enum {
    RESP_EVT_NONE       = 0,
    RESP_EVT_APNEA_LIKE = 1,   /* 疑似呼吸暂停：呼吸幅度下降 ≥90%，持续 ≥10s */
    RESP_EVT_HYPOPNEA   = 2,   /* 疑似低通气：呼吸幅度下降 30%~90%，持续 ≥10s */
    RESP_EVT_SHALLOW    = 3,   /* 呼吸偏浅：幅度偏低但未达低通气标准 */
    RESP_EVT_RECOVERY   = 4,   /* 恢复呼吸：异常后幅度反弹 */
} resp_event_type_t;

/* ================================================================
 * SnoreRespScore 输入 —— 纯雷达 + 血氧特征
 * ================================================================ */
typedef struct {
    /* ── 雷达呼吸事件统计 ── */
    float    sleep_hours;             /* 有效睡眠时长（小时） */
    uint16_t apnea_like_count;        /* 呼吸暂停疑似次数 */
    uint16_t hypopnea_like_count;     /* 低通气疑似次数 */
    uint16_t total_event_count;       /* apnea + hypopnea */
    float    rrei;                    /* 雷达呼吸事件指数 = total_event / sleep_hours */

    /* ── 雷达自带上报 ── */
    uint16_t radar_apnea_10min;       /* 雷达 composite 0x84 0x0C：apnea_count_10min */
    uint8_t  radar_breath_status;     /* 0正常 1过低 2过高 */
    float    avg_breath_rate;         /* 平均呼吸率 */
    float    min_breath_rate;         /* 最低呼吸率 */

    /* ── 血氧 ── */
    bool     has_spo2;
    float    avg_spo2;
    float    min_spo2;
    float    t90_ratio;               /* SpO2 < 90% 时间占比 */
    uint16_t spo2_drop_count;         /* SpO2 下降 ≥3% 次数 */

    /* ── 自主神经代偿（心率） ── */
    bool     has_hr;
    float    max_delta_hr_after_event; /* 事件后心率最大上升幅度（bpm） */
    bool     no_hr_response_flag;      /* 呼吸中断但心率无明显变化（疑似中枢特征） */
    float    avg_heart_rate;

    /* ── 体动 ── */
    uint16_t motion_count;
    uint16_t large_motion_count;
    uint16_t turn_over_count;         /* 翻身次数（来自雷达 composite） */

    /* ── 数据可用性 ── */
    bool     has_radar;
    bool     has_breath_wave_data;     /* 是否有呼吸波形数据用于事件检测 */
} SnoreRespFeatures;

/* ================================================================
 * SnoreRespScore 输出
 * ================================================================ */
typedef enum {
    RISK_GRADE_0_PERFECT    = 0,  /* 90~100：生理性/完美 */
    RISK_GRADE_1_MILD       = 1,  /* 75~89：轻微/观察级 */
    RISK_GRADE_2_MODERATE   = 2,  /* 50~74：中度/建议关注 */
    RISK_GRADE_3_SEVERE     = 3,  /* 30~49：重度/建议就医评估 */
    RISK_GRADE_4_CRITICAL   = 4,  /* 0~29：极高风险/建议尽快医学评估 */
} risk_grade_t;

typedef enum {
    PATH_NONE                   = 0,
    PATH_POSITION_AGGRAVATED    = 1,  /* 体位加重倾向（频繁翻身伴随事件） */
    PATH_HYPOPNEA_DOMINANT      = 2,  /* 低通气为主倾向 */
    PATH_OBSTRUCTIVE_LIKE       = 3,  /* 阻塞倾向（心率反跳明显），非医学诊断 */
    PATH_CENTRAL_LIKE           = 4,  /* 中枢倾向（心率无代偿），非医学诊断 */
    PATH_MILD_PHYSIOLOGICAL     = 5,  /* 生理性波动 */
    PATH_UNCLASSIFIED           = 6,  /* 有病理性信号但不符合已知亚型 */
} pathology_subtype_t;

typedef struct {
    /* ── 总分 ── */
    int     score;                     /* 0~100 */
    risk_grade_t risk_grade;
    const char *risk_level_str;       /* "完美"/"观察级"/"建议关注"/"建议就医"/"建议尽快评估" */

    /* ── 子维度得分 ── */
    int     event_load_score;          /* 呼吸事件负荷 (0~50) */
    int     hypoxia_score;             /* 低氧负荷 (0~35) */
    int     autonomic_score;           /* 自主神经代偿 (0~15) */

    /* ── 病理判定 ── */
    bool    pathological_flag;         /* 是否存在病理性风险信号 */
    pathology_subtype_t subtype;
    const char *subtype_str;

    /* ── 强制标记 ── */
    bool    hypoxia_override;          /* 低氧触发强制升级 */
    bool    autonomic_stress_flag;     /* 心率代偿应激 */
    bool    central_pattern_flag;      /* 疑似中枢特征 */

    /* ── 可信度 ── */
    const char *confidence;            /* "medium"/"low" */
    const char *data_completeness;    /* "full"/"missing_spo2"/"missing_hr" */
    bool    spo2_available;

    /* ── 人类可读 ── */
    char    main_reason[160];
    char    suggestion[256];
    char    disclaimer[128];
} SnoreRespResult;

/* ================================================================
 * API
 * ================================================================ */

/**
 * @brief 计算呼吸事件指数 rREI
 * @param event_count  呼吸事件总数
 * @param sleep_hours  有效睡眠小时数
 * @return rREI (次/小时)
 */
float calc_rrei(uint16_t event_count, float sleep_hours);

/**
 * @brief 主评分函数
 * @param f  输入特征（雷达 + 血氧）
 * @return   评分结果
 */
SnoreRespResult CalcSnoreRespRisk(const SnoreRespFeatures *f);

/**
 * @brief 风险等级枚举 → 中文标签
 */
const char *risk_grade_to_str(risk_grade_t g);

/* ================================================================
 * 辅助：从呼吸波形 ring buffer 实时检测呼吸事件
 * （由 sleep_fusion tick 或独立任务调用）
 * ================================================================ */

/**
 * @brief 基于呼吸波形幅度判断当前是否为呼吸事件
 * @param breath_amp_current   当前呼吸幅度
 * @param breath_amp_baseline  基线幅度（30秒均值）
 * @param duration_sec         当前状态已持续秒数
 * @return 事件类型
 */
resp_event_type_t detect_breath_event(float breath_amp_current,
                                       float breath_amp_baseline,
                                       uint16_t duration_sec);

#ifdef __cplusplus
}
#endif
