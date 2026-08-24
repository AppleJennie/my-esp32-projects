/**
 * sleep_health_fusion.c — 音频鼾声 + 雷达呼吸 融合分析
 *
 * 免责声明：本系统为非接触式睡眠监测与风险提示设备，结果仅用于
 * 睡眠健康趋势参考，不作为医学诊断依据。
 */
#include "sleep_health_fusion.h"
#include "sleep_baseline.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

static const char *TAG = "HLTH_FUSION";

static health_fusion_result_t s_result;

/* ── 体动检测辅助 ── */
static int  s_motion_streak = 0;
static int  s_still_streak  = 0;

/* ── 呼吸下降辅助 ── */
static float s_amp_ring[10];  /* 10 秒窗口 */
static int   s_amp_idx = 0;
static int   s_amp_cnt = 0;

/* ── 事件计数 ── */
static uint32_t s_shallow_count  = 0;
static uint32_t s_pause_count    = 0;
static uint32_t s_recovery_count = 0;
static breath_event_type_t s_last_breath_evt_for_count = BREATH_EVT_UNKNOWN;

void sleep_health_fusion_init(void)
{
    memset(&s_result, 0, sizeof(s_result));
    memset(s_amp_ring, 0, sizeof(s_amp_ring));
    s_amp_idx = s_amp_cnt = 0;
    s_motion_streak = s_still_streak = 0;
    s_shallow_count = s_pause_count = s_recovery_count = 0;
    s_last_breath_evt_for_count = BREATH_EVT_UNKNOWN;
    snore_event_detector_init();
    radar_breath_event_init();
    ESP_LOGI(TAG, "Health fusion init");
}

void sleep_health_fusion_tick(const audio_feature_t *af,
                               const radar_feature_t *rf,
                               const baseline_t *bl,
                               uint32_t now_ms)
{
    memset(&s_result, 0, sizeof(s_result));
    s_result.timestamp_ms = now_ms;

    /* ── 喂子模块 ── */
    snore_event_detector_feed(af, now_ms);
    if (rf) radar_breath_event_feed(rf, bl, now_ms);

    const breath_event_t *be = radar_breath_event_get();
    snore_stats_t ss;
    snore_event_get_stats(&ss);

    /* ── 基本数据填充 ── */
    if (af) {
        s_result.is_snoring  = af->is_snoring;
        s_result.snore_score = (uint8_t)(af->snore_prob * 255.0f);
        s_result.snore_prob  = af->snore_prob;
        s_result.rms         = af->rms_energy;
        s_result.peak        = (float)af->peak;
    }
    if (rf) {
        s_result.breath_bpm  = rf->breath_valid ? rf->breath_bpm : 0;
        s_result.heart_bpm   = rf->heart_valid ? rf->heart_bpm : 0;
        s_result.body_motion = rf->body_motion;
        s_result.breath_amp  = rf->breath_wave_valid ? rf->breath_amp : 0;
        s_result.in_bed      = rf->bed_valid && rf->in_bed;
        s_result.presence    = rf->presence_valid ? rf->presence : rf->presence_inferred;
    }
    s_result.snore_episode_count   = ss.episode_count_total;
    s_result.snore_duration_total_ms = ss.total_duration_ms;
    s_result.longest_snore_ms      = ss.longest_episode_ms;
    s_result.snore_burden_pct      = ss.snore_burden_pct;
    s_result.shallow_breath_events = s_shallow_count;
    s_result.pause_suspected_events = s_pause_count;
    s_result.recovery_events       = s_recovery_count;

    /* ── 数据有效性 ── */
    bool radar_ok = rf && rf->radar_connected && (rf->breath_valid || rf->breath_wave_valid);
    bool audio_ok = af && af->audio_valid;

    if (!radar_ok && !audio_ok) {
        /* 完全无有效数据 */
        s_result.snore_class = SNORE_CLASS_NONE;
        s_result.health_risk = HLTH_RISK_DATA_LOW_QUALITY;
        s_result.confidence = 10;
        snprintf(s_result.event_name, sizeof(s_result.event_name), "数据不足");
        snprintf(s_result.suggestion, sizeof(s_result.suggestion),
                 "麦克风和雷达均无有效数据，请检查设备。");
        return;
    }

    if (!radar_ok && audio_ok) {
        /* ── Audio-only 模式：仅分析鼾声，不做呼吸暂停/低通气结论 ── */
        if (af->is_snoring && af->model_valid && af->snore_prob >= 0.5f) {
            /* 有鼾声 → 分类鼾声类型 */
            float lf  = af->low_freq_ratio;
            float har = af->harmonic_ratio;
            float zcr = af->zcr;
            float rms = af->rms_energy;

            if (zcr > 0.12f && har < 0.3f && rms < 200.0f) {
                s_result.snore_class = SNORE_CLASS_MOUTH_BREATH_LIKE;
                snprintf(s_result.event_name, sizeof(s_result.event_name), "疑似口呼吸(audio-only)");
            } else if (lf > 0.4f && har > 0.3f) {
                s_result.snore_class = SNORE_CLASS_THROAT_SNORE_LIKE;
                snprintf(s_result.event_name, sizeof(s_result.event_name), "疑似喉鼾(audio-only)");
            } else if (lf > 0.3f || har > 0.5f) {
                s_result.snore_class = SNORE_CLASS_OBSTRUCTIVE_RISK;
                snprintf(s_result.event_name, sizeof(s_result.event_name), "鼾声明显(audio-only)");
            } else {
                s_result.snore_class = SNORE_CLASS_SIMPLE;
                snprintf(s_result.event_name, sizeof(s_result.event_name), "普通鼾声(audio-only)");
            }
            s_result.health_risk = HLTH_RISK_LIGHT_SNORE;
            s_result.confidence  = (uint8_t)(af->snore_prob * 80.0f);
            if (s_result.confidence > 70) s_result.confidence = 70;
        } else {
            s_result.snore_class = SNORE_CLASS_NONE;
            s_result.health_risk = HLTH_RISK_NORMAL;
            s_result.confidence  = 30;
            snprintf(s_result.event_name, sizeof(s_result.event_name), "正常(audio-only)");
        }
        snprintf(s_result.suggestion, sizeof(s_result.suggestion),
                 "雷达未启用，仅监测鼾声。呼吸暂停/低通气风险待接入雷达后评估。");
        return;
    }

    /* ═══ 以下为 radar_ok 的正常融合路径 ═══ */

    /* ── 运动趋势 ── */
    if (rf && rf->body_motion > 20) s_motion_streak++;
    else { s_motion_streak = 0; s_still_streak++; if (s_still_streak > 60) s_still_streak = 60; }

    /* ── 呼吸幅度趋势 ── */
    if (rf && rf->breath_wave_valid) {
        s_amp_ring[s_amp_idx] = rf->breath_amp;
        s_amp_idx = (s_amp_idx + 1) % 10;
        if (s_amp_cnt < 10) s_amp_cnt++;
    }

    /* 10 秒趋势 */
    float amp_trend = 0;
    if (s_amp_cnt >= 5) {
        float old_sum = 0, new_sum = 0;
        for (int i = 0; i < 5; i++) {
            old_sum += s_amp_ring[(s_amp_idx - 10 + i + 20) % 10];
            new_sum += s_amp_ring[(s_amp_idx - 5 + i + 20) % 10];
        }
        if (old_sum > 0) amp_trend = 1.0f - (new_sum / old_sum);
    }

    breath_event_type_t be_type = be ? be->type : BREATH_EVT_UNKNOWN;
    bool breath_evt_edge = (be_type != s_last_breath_evt_for_count);
    s_last_breath_evt_for_count = be_type;

    /* ═══════════════════════════════════════════════════
     * 融合规则
     * ═══════════════════════════════════════════════════ */

    /* 体动伪影优先 */
    if (rf && (rf->body_motion > 30 || s_motion_streak >= 3)) {
        s_result.snore_class = SNORE_CLASS_MOVEMENT_ARTIFACT;
        s_result.health_risk = HLTH_RISK_DATA_LOW_QUALITY;
        s_result.confidence  = 60;
        snprintf(s_result.event_name, sizeof(s_result.event_name), "体动干扰");
        snprintf(s_result.suggestion, sizeof(s_result.suggestion),
                 "当前体动较大，暂停呼吸分析。");
        return;
    }

    /* 无鼾声 → 正常 */
    if (!af || !af->is_snoring) {
        /* 但有呼吸异常 */
        if (be->type == BREATH_EVT_PAUSE_SUSPECTED) {
            if (breath_evt_edge) s_pause_count++;
            s_result.snore_class = SNORE_CLASS_NONE;
            s_result.health_risk = HLTH_RISK_APNEA_SUSPECTED;
            s_result.confidence  = be->confidence;
            snprintf(s_result.event_name, sizeof(s_result.event_name),
                     "疑似呼吸暂停风险");
            snprintf(s_result.suggestion, sizeof(s_result.suggestion),
                     "雷达检测到呼吸幅度显著下降，建议关注。如长期出现，建议进行专业睡眠监测。");
            return;
        }
        if (be->type == BREATH_EVT_SHALLOW) {
            if (breath_evt_edge) s_shallow_count++;
            s_result.snore_class = SNORE_CLASS_NONE;
            s_result.health_risk = HLTH_RISK_HYPOPNEA_SUSPECTED;
            s_result.confidence  = be->confidence;
            snprintf(s_result.event_name, sizeof(s_result.event_name),
                     "疑似低通气风险");
            snprintf(s_result.suggestion, sizeof(s_result.suggestion),
                     "雷达检测到呼吸变浅，建议关注。");
            return;
        }
        if (be->type == BREATH_EVT_RECOVERY) {
            if (breath_evt_edge) s_recovery_count++;
        }

        s_result.snore_class = SNORE_CLASS_NONE;
        s_result.health_risk = HLTH_RISK_NORMAL;
        s_result.confidence  = 50;
        snprintf(s_result.event_name, sizeof(s_result.event_name), "正常");
        snprintf(s_result.suggestion, sizeof(s_result.suggestion),
                 "当前未检测到明显异常。");
        return;
    }

    /* ── 有鼾声 → 分析类型 ── */
    float lf  = af->low_freq_ratio;
    float har = af->harmonic_ratio;
    float rms = af->rms_energy;
    float zcr = af->zcr;

    /* 口呼吸/气流声倾向：zcr 偏高，harmonic 不高 */
    if (zcr > 0.12f && har < 0.3f && rms < 200.0f) {
        s_result.snore_class = SNORE_CLASS_MOUTH_BREATH_LIKE;
        s_result.health_risk = HLTH_RISK_LIGHT_SNORE;
        s_result.confidence  = 40;
        snprintf(s_result.event_name, sizeof(s_result.event_name),
                 "疑似口呼吸");
        snprintf(s_result.suggestion, sizeof(s_result.suggestion),
                 "疑似张口呼吸或气流声，建议关注睡眠姿势和鼻腔通气。");
        return;
    }

    /* 阻塞倾向：低频明显 + high harmonic + 呼吸下降 */
    if ((lf > 0.3f || har > 0.5f) && amp_trend > 0.2f && be->type != BREATH_EVT_STABLE) {
        s_result.snore_class = SNORE_CLASS_OBSTRUCTIVE_RISK;
        s_result.health_risk = HLTH_RISK_BREATH_RESTRICTION;
        s_result.confidence  = (uint8_t)(40 + amp_trend * 80);
        if (s_result.confidence > 75) s_result.confidence = 75;
        snprintf(s_result.event_name, sizeof(s_result.event_name),
                 "疑似阻塞倾向");
        snprintf(s_result.suggestion, sizeof(s_result.suggestion),
                 "鼾声特征和呼吸幅度变化提示可能存在上气道阻塞倾向，建议结合睡眠姿势调整。如长期出现，建议进一步筛查。");
        return;
    }

    /* 喉鼾倾向：低频高，harmonic 中等 */
    if (lf > 0.4f && har > 0.3f) {
        s_result.snore_class = SNORE_CLASS_THROAT_SNORE_LIKE;
        s_result.health_risk = HLTH_RISK_LIGHT_SNORE;
        s_result.confidence  = 45;
        snprintf(s_result.event_name, sizeof(s_result.event_name),
                 "疑似喉鼾");
        snprintf(s_result.suggestion, sizeof(s_result.suggestion),
                 "鼾声特征提示可能为喉部组织振动引起。");
        return;
    }

    /* 默认普通鼾声 */
    s_result.snore_class = SNORE_CLASS_SIMPLE;
    s_result.health_risk = HLTH_RISK_LIGHT_SNORE;
    s_result.confidence  = 50;
    snprintf(s_result.event_name, sizeof(s_result.event_name), "普通鼾声");
    snprintf(s_result.suggestion, sizeof(s_result.suggestion),
             "单纯鼾声，当前未提示明显呼吸异常。");
}

const health_fusion_result_t *sleep_health_fusion_get(void) { return &s_result; }

const char *snore_class_label(snore_class_t c) {
    switch (c) {
        case SNORE_CLASS_SIMPLE:             return "普通";
        case SNORE_CLASS_MOUTH_BREATH_LIKE:  return "气流";
        case SNORE_CLASS_THROAT_SNORE_LIKE:  return "喉鼾";
        case SNORE_CLASS_OBSTRUCTIVE_RISK:   return "阻塞倾向";
        case SNORE_CLASS_MOVEMENT_ARTIFACT:  return "体动";
        case SNORE_CLASS_MIXED:              return "混合";
        default:                             return "无";
    }
}

const char *health_risk_label(health_risk_t r) {
    switch (r) {
        case HLTH_RISK_NORMAL:                       return "正常";
        case HLTH_RISK_LIGHT_SNORE:                  return "轻度";
        case HLTH_RISK_BREATH_RESTRICTION: return "疑似受限";
        case HLTH_RISK_HYPOPNEA_SUSPECTED:           return "疑似低通气";
        case HLTH_RISK_APNEA_SUSPECTED:              return "疑似暂停";
        case HLTH_RISK_DATA_LOW_QUALITY:             return "数据不足";
        default:                                return "未知";
    }
}
