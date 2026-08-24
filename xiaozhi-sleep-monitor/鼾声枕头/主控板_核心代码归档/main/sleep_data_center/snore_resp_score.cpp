/**
 * snore_resp_score.cpp — 呼吸暂停/低通气专项风险评估（纯雷达+血氧，无音频）
 *
 * 评分维度（100 分）：
 *   A. 呼吸事件负荷    50 分
 *   B. 低氧负荷         35 分
 *   C. 自主神经代偿     15 分
 *
 * 强制规则：
 *   - rREI ≥ 5 或 min_spo2 < 90% → pathological_flag = true
 *   - min_spo2 < 85% 或 T90 > 10% → 风险至少 grade 3
 *   - min_spo2 < 90% → 风险上调一级
 *
 * 免责声明：本结果仅作为家庭睡眠观察参考，不能替代医学诊断。
 */

#include "snore_resp_score.h"
#include <string.h>
#include <stdio.h>

extern "C" {

/* ═══════════════════════════════════════════════════════════════
 * 内部辅助
 * ═══════════════════════════════════════════════════════════════ */

static inline int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ═══════════════════════════════════════════════════════════════
 * rREI 计算
 * ═══════════════════════════════════════════════════════════════ */

float calc_rrei(uint16_t event_count, float sleep_hours)
{
    if (sleep_hours < 0.05f) return 0.0f;
    return (float)event_count / sleep_hours;
}

/* ═══════════════════════════════════════════════════════════════
 * A. 呼吸事件负荷评分（50 分）
 *
 * 使用 rREI（雷达呼吸事件指数）。
 *
 * rREI 来源两个通道取最大值：
 *   通道1: 本地波形检测的 apnea_like + hypopnea_like 换算
 *   通道2: 雷达自带上报 radar_apnea_10min 折算
 *
 * 评分曲线：
 *   rREI = 0           → 50 分
 *   0 < rREI ≤ 5       → 45 分  (正常范围上限)
 *   5 < rREI ≤ 15      → 45→30   (轻度)
 *   15 < rREI ≤ 30     → 30→10   (中度)
 *   rREI > 30          → 10→0    (重度)
 * ═══════════════════════════════════════════════════════════════ */

static int score_event_load(float rrei)
{
    if (rrei <= 0.0f) return 50;
    if (rrei <= 5.0f) return 45;
    if (rrei <= 15.0f) {
        /* 45 → 30，线性 */
        return 45 - (int)((rrei - 5.0f) / 10.0f * 15.0f);
    }
    if (rrei <= 30.0f) {
        /* 30 → 10，线性 */
        return 30 - (int)((rrei - 15.0f) / 15.0f * 20.0f);
    }
    /* > 30：缓慢衰减到底 */
    int s = 10 - (int)((rrei - 30.0f) * 0.33f);
    return s > 0 ? s : 0;
}

/* ═══════════════════════════════════════════════════════════════
 * B. 低氧负荷评分（35 分）
 *
 * 核心指标：min_spo2, T90
 *
 *   min_spo2 ≥ 95%             → 35 分
 *   90% ≤ min_spo2 < 95%       → 28 分
 *   88% ≤ min_spo2 < 90%       → 18 分
 *   85% ≤ min_spo2 < 88%       → 10 分
 *   min_spo2 < 85%             → 0~6 分
 *
 *   T90 > 10%                  → 低氧评分直接归 0
 *
 * 无血氧 → 默认 21 分（中性，不偏高也不偏低）
 * ═══════════════════════════════════════════════════════════════ */

static int score_hypoxia(bool has_spo2, float min_spo2, float t90_ratio)
{
    if (!has_spo2) {
        return 21;  /* 无血氧，中性分 */
    }

    /* T90 惩罚：血氧 < 90% 时间占比 > 10% → 直接归零 */
    if (t90_ratio > 0.10f) {
        return 0;
    }

    if (min_spo2 >= 95.0f) return 35;
    if (min_spo2 >= 90.0f) return 28;
    if (min_spo2 >= 88.0f) return 18;
    if (min_spo2 >= 85.0f) return 10;

    /* < 85%：每降 1% 扣 1.2 分，最低 0 */
    int s = 6 - (int)((85.0f - min_spo2) * 1.2f);
    return s > 0 ? s : 0;
}

/* ═══════════════════════════════════════════════════════════════
 * C. 自主神经代偿评分（15 分）
 *
 * 看呼吸事件前后心率变化：
 *
 *   ΔHR ≤ 10 bpm              → 15 分（正常波动范围）
 *   10 < ΔHR ≤ 20 bpm          → 10 分（存在轻度代偿）
 *   ΔHR > 20 bpm               →  5 分（交感神经应激明显）
 *
 *   事件中 HR 无明显变化（且无鼾声确认）
 *     → 0 分（需标记中枢/混合型风险）
 *
 * 无心率数据 → 默认 8 分
 * ═══════════════════════════════════════════════════════════════ */

static int score_autonomic(bool has_hr, float max_delta_hr,
                            bool no_hr_response_flag)
{
    if (!has_hr) {
        return 8;  /* 无心率，中性分 */
    }

    /* 中枢特征：呼吸中断但心率不代偿 */
    if (no_hr_response_flag) {
        return 0;
    }

    if (max_delta_hr <= 10.0f) return 15;
    if (max_delta_hr <= 20.0f) return 10;
    return 5;
}

/* ═══════════════════════════════════════════════════════════════
 * 呼吸事件检测（基于波形幅度）
 * ═══════════════════════════════════════════════════════════════ */

resp_event_type_t detect_breath_event(float breath_amp_current,
                                       float breath_amp_baseline,
                                       uint16_t duration_sec)
{
    if (breath_amp_baseline < 1.0f || duration_sec < 10) {
        return RESP_EVT_NONE;
    }

    float ratio = breath_amp_current / breath_amp_baseline;

    /* 幅度下降 ≥ 90% + 持续 ≥ 10s → 疑似呼吸暂停 */
    if (ratio <= 0.10f && duration_sec >= 10) {
        return RESP_EVT_APNEA_LIKE;
    }

    /* 幅度下降 30%~90% + 持续 ≥ 10s → 疑似低通气 */
    if (ratio > 0.10f && ratio <= 0.70f && duration_sec >= 10) {
        return RESP_EVT_HYPOPNEA;
    }

    /* 幅度偏低但未达标准 */
    if (ratio > 0.70f && ratio <= 0.85f && duration_sec >= 5) {
        return RESP_EVT_SHALLOW;
    }

    return RESP_EVT_NONE;
}

/* ═══════════════════════════════════════════════════════════════
 * 风险等级 → 字符串
 * ═══════════════════════════════════════════════════════════════ */

const char *risk_grade_to_str(risk_grade_t g)
{
    switch (g) {
        case RISK_GRADE_0_PERFECT:   return "完美";
        case RISK_GRADE_1_MILD:      return "观察级";
        case RISK_GRADE_2_MODERATE:  return "建议关注";
        case RISK_GRADE_3_SEVERE:    return "建议就医";
        case RISK_GRADE_4_CRITICAL:  return "建议尽快评估";
        default:                     return "未知";
    }
}

static const char *subtype_to_str(pathology_subtype_t s)
{
    switch (s) {
        case PATH_POSITION_AGGRAVATED:  return "体位加重型疑似";
        case PATH_HYPOPNEA_DOMINANT:    return "低通气为主型";
        case PATH_OBSTRUCTIVE_LIKE:   return "阻塞倾向（非医学诊断）";
        case PATH_CENTRAL_LIKE:    return "中枢倾向（非医学诊断）";
        case PATH_MILD_PHYSIOLOGICAL:   return "生理性波动";
        case PATH_UNCLASSIFIED:         return "未分类异常";
        default:                        return "正常";
    }
}

/* ═══════════════════════════════════════════════════════════════
 * 主评分函数
 * ═══════════════════════════════════════════════════════════════ */

SnoreRespResult CalcSnoreRespRisk(const SnoreRespFeatures *f)
{
    SnoreRespResult r;
    memset(&r, 0, sizeof(r));

    /* ── 无雷达数据 → 直接返回 ── */
    if (!f || !f->has_radar) {
        r.score = 0;
        r.risk_grade = RISK_GRADE_4_CRITICAL;
        r.risk_level_str = "数据不足";
        r.confidence = "low";
        r.spo2_available = f ? f->has_spo2 : false;
        snprintf(r.main_reason, sizeof(r.main_reason),
                 "无雷达数据，无法评估呼吸风险");
        snprintf(r.suggestion, sizeof(r.suggestion),
                 "请确认雷达设备已连接并正常工作");
        snprintf(r.disclaimer, sizeof(r.disclaimer),
                 "本结果仅作为家庭睡眠观察参考，不能替代医学诊断。");
        return r;
    }

    /* ── 计算实际使用的 rREI ── */
    /* 优先用本地波形检测的事件数；如果本地检测不到但雷达有上报，用雷达的 */
    float rrei;

    if (f->has_breath_wave_data && f->total_event_count > 0) {
        /* 本地波形检测到了事件 */
        rrei = calc_rrei(f->total_event_count, f->sleep_hours);
    } else if (f->radar_apnea_10min > 0) {
        /* 使用雷达自带上报的 apnea_count（10分钟窗口），粗略外推为每小时 */
        rrei = (float)f->radar_apnea_10min * 6.0f;  /* 注意：外推假设 10min 窗口代表整晚 */
    } else {
        /* 没有任何事件数据 → rREI=0 */
        rrei = 0.0f;
    }

    /* ── A. 呼吸事件负荷（50 分） ── */
    int event_score = score_event_load(rrei);

    /* ── B. 低氧负荷（35 分） ── */
    int hypox_score = score_hypoxia(f->has_spo2, f->min_spo2, f->t90_ratio);

    /* ── C. 自主神经代偿（15 分） ── */
    int auton_score = score_autonomic(f->has_hr, f->max_delta_hr_after_event,
                                       f->no_hr_response_flag);

    r.event_load_score = clampi(event_score, 0, 50);
    r.hypoxia_score    = clampi(hypox_score, 0, 35);
    r.autonomic_score  = clampi(auton_score, 0, 15);

    int raw_score = r.event_load_score + r.hypoxia_score + r.autonomic_score;

    /* ── 数据完整性标记, 不做硬归一化(避免溢出) ── */
    if (!f->has_spo2) {
        r.data_completeness = "missing_spo2";
    } else if (!f->has_hr) {
        r.data_completeness = "missing_hr";
    } else {
        r.data_completeness = "full";
    }
    r.score = clampi(raw_score, 0, 100);

    /* ── 可信度 ── */
    r.spo2_available = f->has_spo2;
    if (f->has_spo2 && f->has_hr) {
        r.confidence = "medium";
    } else {
        r.confidence = "low";
    }

    /* ═══════════════════════════════════════════════════════════
     * 病理判定
     * ═══════════════════════════════════════════════════════════ */

    /* 病理性风险分水岭 */
    r.pathological_flag = false;
    if (rrei >= 5.0f) {
        r.pathological_flag = true;
    }
    if (f->has_spo2 && f->min_spo2 < 90.0f) {
        r.pathological_flag = true;
    }
    /* 至少 1 次呼吸中断 + 血氧下降 ≥3% */
    if (f->has_spo2 && f->apnea_like_count >= 1 && f->spo2_drop_count >= 1) {
        r.pathological_flag = true;
    }

    /* 自主神经应激标记 */
    r.autonomic_stress_flag = (f->has_hr && f->max_delta_hr_after_event > 20.0f);

    /* 中枢模式标记 */
    r.central_pattern_flag = (f->has_hr && f->no_hr_response_flag
                               && f->apnea_like_count >= 1);

    /* ═══════════════════════════════════════════════════════════
     * 风险等级
     * ═══════════════════════════════════════════════════════════ */

    if (r.score >= 90)       r.risk_grade = RISK_GRADE_0_PERFECT;
    else if (r.score >= 75)  r.risk_grade = RISK_GRADE_1_MILD;
    else if (r.score >= 50)  r.risk_grade = RISK_GRADE_2_MODERATE;
    else if (r.score >= 30)  r.risk_grade = RISK_GRADE_3_SEVERE;
    else                     r.risk_grade = RISK_GRADE_4_CRITICAL;

    /* ── 强制规则：血氧风险上调 ── */
    r.hypoxia_override = false;
    if (f->has_spo2) {
        if (f->min_spo2 < 85.0f || f->t90_ratio > 0.10f) {
            /* 至少 grade 3 */
            if (r.risk_grade < RISK_GRADE_3_SEVERE) {
                r.risk_grade = RISK_GRADE_3_SEVERE;
                r.hypoxia_override = true;
            }
        } else if (f->min_spo2 < 90.0f) {
            /* 上调一级 */
            if (r.risk_grade < RISK_GRADE_4_CRITICAL) {
                r.risk_grade = (risk_grade_t)((int)r.risk_grade + 1);
                r.hypoxia_override = true;
            }
        }
    }

    r.risk_level_str = risk_grade_to_str(r.risk_grade);

    /* ═══════════════════════════════════════════════════════════
     * 病理亚型分类
     * ═══════════════════════════════════════════════════════════ */

    if (!r.pathological_flag) {
        r.subtype = PATH_MILD_PHYSIOLOGICAL;
    } else {
        /* 中枢特征优先 */
        if (r.central_pattern_flag) {
            r.subtype = PATH_CENTRAL_LIKE;
        }
        /* 重度阻塞：ΔHR > 20 + 有呼吸暂停事件 */
        else if (r.autonomic_stress_flag && f->apnea_like_count >= 1) {
            r.subtype = PATH_OBSTRUCTIVE_LIKE;
        }
        /* 低通气为主：hypopnea 占多数 */
        else if (f->hypopnea_like_count > f->apnea_like_count * 3) {
            r.subtype = PATH_HYPOPNEA_DOMINANT;
        }
        /* 体位加重：翻身多 + 有事件 */
        else if (f->turn_over_count > 20 && f->total_event_count > 0) {
            r.subtype = PATH_POSITION_AGGRAVATED;
        }
        /* 有病理性信号但不匹配已知亚型 */
        else {
            r.subtype = PATH_UNCLASSIFIED;
        }
    }
    r.subtype_str = subtype_to_str(r.subtype);

    /* ═══════════════════════════════════════════════════════════
     * 生成原因和建议
     * ═══════════════════════════════════════════════════════════ */

    char *reason = r.main_reason;
    size_t rem = sizeof(r.main_reason);
    int pos = 0;

    if (!f->has_spo2) {
        pos += snprintf(reason + pos, rem - pos,
                        "未接入血氧，基于雷达呼吸数据评估。");
    }

    if (rrei >= 15.0f) {
        pos += snprintf(reason + pos, rem - pos,
                        "呼吸事件频繁(rREI=%.1f)，", rrei);
    } else if (rrei >= 5.0f) {
        pos += snprintf(reason + pos, rem - pos,
                        "存在呼吸事件(rREI=%.1f)，", rrei);
    }

    if (f->has_spo2 && f->min_spo2 < 90.0f) {
        pos += snprintf(reason + pos, rem - pos,
                        "最低血氧%.0f%%，", f->min_spo2);
    }

    if (r.autonomic_stress_flag) {
        pos += snprintf(reason + pos, rem - pos,
                        "事件后心率明显反跳(ΔHR=%.0fbpm)，", f->max_delta_hr_after_event);
    }

    if (r.central_pattern_flag) {
        pos += snprintf(reason + pos, rem - pos,
                        "呼吸中断时心率无明显代偿，需注意非典型模式，");
    }

    if (pos == 0) {
        /* 无特定风险 → 默认描述 */
        snprintf(reason, rem, "当前呼吸和血氧指标总体平稳");
    }

    /* ── 建议 ── */
    char *sug = r.suggestion;
    size_t srem = sizeof(r.suggestion);
    int spos = 0;

    switch (r.risk_grade) {
        case RISK_GRADE_0_PERFECT:
            spos += snprintf(sug + spos, srem - spos,
                             "呼吸状态良好，请保持规律作息。");
            break;
        case RISK_GRADE_1_MILD:
            spos += snprintf(sug + spos, srem - spos,
                             "存在轻微呼吸波动，建议连续观察3晚趋势。");
            if (r.subtype == PATH_POSITION_AGGRAVATED) {
                spos += snprintf(sug + spos, srem - spos,
                                 "可尝试调整睡姿。");
            }
            break;
        case RISK_GRADE_2_MODERATE:
            spos += snprintf(sug + spos, srem - spos,
                             "建议关注呼吸和血氧变化，连续监测观察趋势。");
            if (r.subtype == PATH_HYPOPNEA_DOMINANT) {
                spos += snprintf(sug + spos, srem - spos,
                                 "注意卧室通风和睡眠姿势。");
            }
            if (!f->has_spo2) {
                spos += snprintf(sug + spos, srem - spos,
                                 "建议接入血氧设备以提高评估准确性。");
            }
            spos += snprintf(sug + spos, srem - spos,
                             "如长期出现，建议咨询医生。");
            break;
        case RISK_GRADE_3_SEVERE:
            spos += snprintf(sug + spos, srem - pos,
                             "检测到较强呼吸风险信号，建议进行专业睡眠评估。");
            if (f->has_spo2 && f->min_spo2 < 88.0f) {
                spos += snprintf(sug + spos, srem - pos,
                                 "夜间血氧偏低需重点关注。");
            }
            break;
        case RISK_GRADE_4_CRITICAL:
            spos += snprintf(sug + spos, srem - pos,
                             "检测到极高呼吸风险信号，建议尽快进行医学评估。");
            break;
    }

    if (r.central_pattern_flag) {
        spos += snprintf(sug + spos, srem - pos,
                         "检测到非典型呼吸中断模式，建议进行专业多导睡眠监测(PSG)。");
    }

    snprintf(r.disclaimer, sizeof(r.disclaimer),
             "本结果仅作为家庭睡眠观察参考，不能替代医学诊断。");

    return r;
}

} /* extern "C" */
