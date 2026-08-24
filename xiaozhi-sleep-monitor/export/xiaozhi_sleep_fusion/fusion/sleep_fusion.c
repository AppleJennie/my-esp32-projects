/**
 * sleep_fusion.c — 音频+雷达多证据融合风险筛查引擎 v3
 *
 * 架构：ring buffer(120s) → window_features(10s/30s) → state_machine → scoring → result
 *
 * 事件规则严格遵循需求：
 *  - normal / simple_snore / suspected_obstructive / suspected_central
 *  - suspected_hypopnea / recovery_breath / movement_artifact / body_movement_arousal
 *
 * 硬性抑制：
 *  - baseline_valid=false → 不允许高置信 apnea/hypopnea
 *  - audio_valid=false → 不允许 obstructive_apnea
 *  - model_valid=false → 不允许 snore 相关风险
 *  - body_motion>20 → 不允许 apnea/hypopnea，优先 movement_artifact
 *  - confidence<60 → 不计入报告，不在首页显示正式风险
 */

#include "sleep_fusion.h"
#include "sleep_baseline.h"
#include "esp_log.h"

#define SNORE_MODEL_TEST_MODE 0  /* 正式模式: 启用融合逻辑 */
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG      = "FUSION";
static const char *FUSION_TAG = "FUSION_RES";

/* ── Ring buffers (120 秒) ── */
static audio_feature_t s_ab[FUSION_RING_SEC]; static int s_ab_idx = 0, s_ab_cnt = 0;
static radar_feature_t s_rb[FUSION_RING_SEC]; static int s_rb_idx = 0, s_rb_cnt = 0;

/* ── 当前帧 ── */
static audio_feature_t s_af; static bool s_af_ok = false;
static radar_feature_t s_rf; static bool s_rf_ok = false;

/* ── 状态机 ── */
static fusion_event_t s_state          = FUSION_NORMAL;
static uint32_t       s_state_start_ms = 0;
static uint32_t       s_last_tick_ms   = 0;
static uint32_t       s_system_start_ms = 0;  /* 融合初始化时间 */

/* ── 事件统计 ── */
static uint16_t s_apnea_total     = 0;
static uint16_t s_hypopnea_total  = 0;
static uint16_t s_recovery_total  = 0;
static uint16_t s_arousal_total   = 0;
static uint16_t s_snore_sec_total = 0;

/* ── 回调 + 队列 ── */
static fusion_callback_t s_cb   = NULL;
static void             *s_user = NULL;
static QueueHandle_t     s_fusion_queue = NULL;  /* 长度 1，最新快照 */
static fusion_result_t   s_last_result;

/* ── 新鲜度阈值 ── */
#define AF_FRESH 1500
#define BR_FRESH 5000
#define HR_FRESH 5000
#define MT_FRESH 3000
#define BW_FRESH 2000

/* ═══════════════════════════════════════════════════════════════
 * 新鲜度检查
 * ═══════════════════════════════════════════════════════════════ */
static bool _af_fresh(uint32_t now) { return s_af_ok && s_af.audio_valid && (now - s_af.timestamp_ms) <= AF_FRESH; }
static bool _br_fresh(uint32_t now) { return s_rf_ok && s_rf.breath_valid && (now - s_rf.timestamp_ms) <= BR_FRESH; }
static bool _hr_fresh(uint32_t now) { return s_rf_ok && s_rf.heart_valid && (now - s_rf.timestamp_ms) <= HR_FRESH; }
static bool _mt_fresh(uint32_t now) { return s_rf_ok && s_rf.motion_valid && (now - s_rf.timestamp_ms) <= MT_FRESH; }
static bool _bw_fresh(uint32_t now) { return s_rf_ok && s_rf.breath_wave_valid && (now - s_rf.timestamp_ms) <= BW_FRESH; }

/* ═══════════════════════════════════════════════════════════════
 * Ring buffer helpers
 * ═══════════════════════════════════════════════════════════════ */
static void _rb_push_audio(const audio_feature_t *a) {
    if (!a) return;
    s_ab[s_ab_idx] = *a;
    s_ab_idx = (s_ab_idx + 1) % FUSION_RING_SEC;
    if (s_ab_cnt < FUSION_RING_SEC) s_ab_cnt++;
}
static void _rb_push_radar(const radar_feature_t *r) {
    if (!r) return;
    s_rb[s_rb_idx] = *r;
    s_rb_idx = (s_rb_idx + 1) % FUSION_RING_SEC;
    if (s_rb_cnt < FUSION_RING_SEC) s_rb_cnt++;
}

static const audio_feature_t *_ab_get(int offset) {
    if (offset >= s_ab_cnt) return NULL;
    int i = (s_ab_idx - 1 - offset + FUSION_RING_SEC) % FUSION_RING_SEC;
    return &s_ab[i];
}
static const radar_feature_t *_rb_get(int offset) {
    if (offset >= s_rb_cnt) return NULL;
    int i = (s_rb_idx - 1 - offset + FUSION_RING_SEC) % FUSION_RING_SEC;
    return &s_rb[i];
}

/* ═══════════════════════════════════════════════════════════════
 * 窗口特征计算（10s / 30s）
 * ═══════════════════════════════════════════════════════════════ */
static void compute_window_features(window_features_t *wf, uint32_t now)
{
    memset(wf, 0, sizeof(window_features_t));

    /* ── 音频窗口 ── */
    int snore_10_cnt = 0, snore_30_cnt = 0, total_10 = 0, total_30 = 0;
    float energy_10 = 0.0f;
    int nasal_cnt = 0, throat_cnt = 0, mouth_cnt = 0;

    for (int i = 0; i < s_ab_cnt; i++) {
        const audio_feature_t *a = _ab_get(i);
        if (!a || !a->audio_valid) continue;
        uint32_t age = now - a->timestamp_ms;
        if (age > 30000) break;

        if (age <= 10000) {
            total_10++;
            if (a->is_snoring) snore_10_cnt++;
            energy_10 += a->rms_energy;
        }
        total_30++;
        if (a->is_snoring) {
            snore_30_cnt++;
            if (a->snore_type == 1) nasal_cnt++;
            else if (a->snore_type == 2) throat_cnt++;
            else if (a->snore_type == 3) mouth_cnt++;
        }
    }

    wf->snore_ratio_10s = total_10 > 0 ? (float)snore_10_cnt / (float)total_10 : 0.0f;
    wf->snore_ratio_30s = total_30 > 0 ? (float)snore_30_cnt / (float)total_30 : 0.0f;
    wf->audio_energy_mean_10s = total_10 > 0 ? energy_10 / (float)total_10 : 0.0f;

    float cur_energy = _af_fresh(now) ? s_af.rms_energy : wf->audio_energy_mean_10s;
    wf->audio_energy_current = cur_energy;
    wf->audio_energy_drop_ratio = (wf->audio_energy_mean_10s > 0.001f && cur_energy > 0)
        ? 1.0f - (cur_energy / wf->audio_energy_mean_10s) : 0.0f;

    wf->recovery_breath_detected = _af_fresh(now) && s_af.recovery_breath_sound;

    if (nasal_cnt >= throat_cnt && nasal_cnt >= mouth_cnt) wf->main_snore_type_30s = 1;
    else if (throat_cnt >= mouth_cnt) wf->main_snore_type_30s = 2;
    else wf->main_snore_type_30s = 3;

    int valid_a_cnt = total_30;
    wf->audio_quality_score = valid_a_cnt > 0 ? (float)valid_a_cnt / (float)(s_ab_cnt < 10 ? 10 : (s_ab_cnt > 30 ? 30 : s_ab_cnt)) : 0.0f;
    if (_af_fresh(now) && s_af.noise_too_high) wf->audio_quality_score *= 0.5f;

    /* ── 雷达窗口 ── */
    float ba_10 = 0.0f, ba_30 = 0.0f, br_30 = 0.0f, hr_30 = 0.0f, mt_max = 0.0f, mt_sum = 0.0f;
    int ba_n10 = 0, ba_n30 = 0, br_n = 0, hr_n = 0, mt_n = 0;
    int bed_ok = 0, pres_ok = 0, bed_total = 0, pres_total = 0;

    for (int i = 0; i < s_rb_cnt; i++) {
        const radar_feature_t *r = _rb_get(i);
        if (!r) continue;
        uint32_t age = now - r->timestamp_ms;
        if (age > 30000) break;

        if (age <= 10000 && r->breath_wave_valid)  { ba_10 += r->breath_amp; ba_n10++; }
        if (r->breath_wave_valid)                   { ba_30 += r->breath_amp; ba_n30++; }
        if (r->breath_valid)                         { br_30 += r->breath_bpm; br_n++; }
        if (r->heart_valid)                          { hr_30 += r->heart_bpm;  hr_n++; }
        if (age <= 10000 && r->motion_valid)         {
            if ((float)r->body_motion > mt_max) mt_max = (float)r->body_motion;
            mt_sum += (float)r->body_motion; mt_n++;
        }
        if (age <= 10000) { bed_total++;  if (r->bed_valid && r->in_bed)    bed_ok++; }
        if (age <= 10000) { pres_total++; if (r->presence_valid || r->presence_inferred) pres_ok++; }
    }

    wf->breath_amp_mean_10s = ba_n10 > 0 ? ba_10 / (float)ba_n10 : 0.0f;
    wf->breath_amp_mean_30s = ba_n30 > 0 ? ba_30 / (float)ba_n30 : 0.0f;
    wf->breath_amp_current  = _bw_fresh(now) ? s_rf.breath_amp : wf->breath_amp_mean_30s;

    const baseline_t *bl = sleep_baseline_get();
    if (bl && bl->valid && bl->breath_amp > 0)
        wf->breath_amp_drop_ratio = 1.0f - (wf->breath_amp_current / bl->breath_amp);
    else if (wf->breath_amp_mean_30s > 0)
        wf->breath_amp_drop_ratio = 1.0f - (wf->breath_amp_current / wf->breath_amp_mean_30s);

    wf->breath_rate_mean_30s = br_n > 0 ? br_30 / (float)br_n : 0.0f;
    wf->heart_rate_mean_30s  = hr_n > 0 ? hr_30 / (float)hr_n : 0.0f;
    wf->heart_rate_current   = _hr_fresh(now) ? s_rf.heart_bpm : wf->heart_rate_mean_30s;
    wf->heart_rate_rise_ratio = (wf->heart_rate_mean_30s > 0)
        ? wf->heart_rate_current / wf->heart_rate_mean_30s : 1.0f;

    wf->motion_max_10s  = mt_max;
    wf->motion_mean_10s = mt_n > 0 ? mt_sum / (float)mt_n : 0.0f;

    wf->radar_quality_score = (ba_n30 + br_n + hr_n) > 0
        ? (float)(ba_n30 + br_n + hr_n) / (float)(ba_n30 + br_n + hr_n + 5) : 0.0f;
    if (!s_rf.radar_connected) wf->radar_quality_score *= 0.3f;

    wf->in_bed_stable   = bed_total > 3 ? (bed_ok >= bed_total * 0.7f) : false;
    wf->presence_stable = pres_total > 3 ? (pres_ok >= pres_total * 0.7f) : false;
}

/* ═══════════════════════════════════════════════════════════════
 * 事件字符串 & 中文标签
 * ═══════════════════════════════════════════════════════════════ */

const char *sleep_fusion_event_label(int event)
{
    switch (event) {
        case FUSION_NORMAL:               return "normal";
        case FUSION_SIMPLE_SNORE:          return "simple_snore";
        case FUSION_SUSPECTED_OBSTRUCTIVE: return "suspected_obstructive_apnea_risk";
        case FUSION_SUSPECTED_CENTRAL:     return "suspected_central_apnea_risk";
        case FUSION_SUSPECTED_HYPOPNEA:    return "suspected_hypopnea_risk";
        case FUSION_RECOVERY_BREATH:       return "recovery_breath";
        case FUSION_MOVEMENT_ARTIFACT:     return "movement_artifact";
        case FUSION_BODY_MOVEMENT_AROUSAL: return "body_movement_arousal";
        case FUSION_DATA_QUALITY_LOW:      return "data_quality_low";
        case FUSION_WARMING_UP:            return "warming_up";
        default:                           return "unknown";
    }
}

/* ═══════════════════════════════════════════════════════════════
 * 初始化 / 外部 API
 * ═══════════════════════════════════════════════════════════════ */

int sleep_fusion_init(fusion_callback_t cb, void *user)
{
    s_cb = cb; s_user = user;
    memset(s_ab, 0, sizeof(s_ab)); s_ab_idx = s_ab_cnt = 0;
    memset(s_rb, 0, sizeof(s_rb)); s_rb_idx = s_rb_cnt = 0;
    memset(&s_af, 0, sizeof(s_af)); s_af_ok = false;
    memset(&s_rf, 0, sizeof(s_rf)); s_rf_ok = false;
    s_state = FUSION_NORMAL; s_state_start_ms = 0;
    s_apnea_total = s_hypopnea_total = s_recovery_total = s_arousal_total = 0;
    s_snore_sec_total = 0;
    s_last_tick_ms = 0;
    s_system_start_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

    s_fusion_queue = xQueueCreate(1, sizeof(fusion_result_t));
    if (!s_fusion_queue) {
        ESP_LOGE(TAG, "Fusion queue create fail");
        return -1;
    }

    sleep_baseline_init();
    ESP_LOGI(TAG, "Fusion v3 initialized");
    return 0;
}

void sleep_fusion_feed_audio(const audio_feature_t *a)
    { if (a) { s_af = *a; s_af_ok = true; _rb_push_audio(a); } }

void sleep_fusion_feed_radar(const radar_feature_t *r)
    { if (r) { s_rf = *r; s_rf_ok = true; _rb_push_radar(r); } }

void sleep_fusion_deinit(void) { s_cb = NULL; }

bool sleep_fusion_get_result(fusion_result_t *out)
{
    if (!out || !s_fusion_queue) return false;
    if (xQueuePeek(s_fusion_queue, out, 0) == pdTRUE) return true;
    return false;
}

void sleep_fusion_get_stats(uint16_t *apnea, uint16_t *hypopnea,
                            uint16_t *recovery, uint16_t *arousal)
{
    if (apnea)    *apnea    = s_apnea_total;
    if (hypopnea) *hypopnea = s_hypopnea_total;
    if (recovery) *recovery = s_recovery_total;
    if (arousal)  *arousal  = s_arousal_total;
}

/* ═══════════════════════════════════════════════════════════════
 * 主 tick：每秒调用一次
 * ═══════════════════════════════════════════════════════════════ */

void sleep_fusion_tick(void)
{
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    s_last_tick_ms = now;

    fusion_result_t out;
    memset(&out, 0, sizeof(out));
    out.timestamp_ms = now;

    /* ── 基本信息 ── */
    out.heart_bpm    = _hr_fresh(now) ? s_rf.heart_bpm : 0;
    out.breath_bpm    = _br_fresh(now) ? s_rf.breath_bpm : 0;
    out.body_motion   = _mt_fresh(now) ? s_rf.body_motion : 0;
    out.breath_status = _br_fresh(now) ? s_rf.breath_status : 0xFF;
    out.presence      = s_rf_ok ? (s_rf.presence_valid ? s_rf.presence : s_rf.presence_inferred) : false;
    out.in_bed        = s_rf_ok ? (s_rf.bed_valid && s_rf.in_bed) : false;
    out.distance_cm   = s_rf_ok ? s_rf.distance_cm : 0;
    out.breath_amp    = _bw_fresh(now) ? s_rf.breath_amp : 0;
    out.heart_amp     = s_rf_ok && s_rf.heart_wave_valid ? s_rf.heart_amp : 0;

    /* ── 鼾声 ── */
    bool af_ok = _af_fresh(now);
    out.snoring    = af_ok && s_af.is_snoring;
    out.snore_type = af_ok ? s_af.snore_type : 0;
    out.snore_prob = af_ok ? s_af.snore_prob : 0;

    /* ── 数据质量 ── */
    bool br_ok = _br_fresh(now) || _bw_fresh(now);
    bool mt_ok = _mt_fresh(now);
    out.data_valid    = br_ok && mt_ok;
    out.audio_quality = af_ok ? (s_af.noise_too_high ? 0.4f : 0.9f) : 0.1f;
    out.radar_quality = s_rf_ok ? (s_rf.radar_connected ? 0.9f : 0.2f) : 0.1f;

    /* ── 基线 ── */
    const baseline_t *bl = sleep_baseline_get();
    out.baseline_valid   = (bl && bl->valid);
    out.baseline_progress = bl ? bl->progress_seconds : 0;

    /* ── 喂基线 ── */
    if (_bw_fresh(now) || _br_fresh(now))
        sleep_baseline_feed(&s_rf, af_ok ? &s_af : NULL);

    /* ── 预热门控（分层）── */
    uint32_t uptime_ms = now - s_system_start_ms;

#if SNORE_MODEL_TEST_MODE
    /* 层0: 鼾声模型测试模式 — 只输出 normal/warming_up */
    out.event = out.baseline_valid ? FUSION_NORMAL : FUSION_WARMING_UP;
    out.confidence = out.baseline_valid ? 30 : 0;
    out.total_score = out.baseline_valid ? 80 : 0;
    snprintf(out.reason, sizeof(out.reason), "snore model test mode");
    goto emit;
#endif

    bool has_vital = _br_fresh(now) || _hr_fresh(now) || _mt_fresh(now);
    bool audio_only = af_ok && !has_vital;
    bool startup_warmup = (uptime_ms < 30000);
    bool baselining = !out.baseline_valid && out.baseline_progress < 120;

    /* 层1: 启动<30s → 预热 */
    if (startup_warmup) {
        out.event = FUSION_WARMING_UP;
        out.confidence = 0;
        snprintf(out.reason, sizeof(out.reason),
                 "系统启动预热中 %lu/30s", (unsigned long)(uptime_ms / 1000));
        goto emit;
    }

    /* 层2: 无音频无雷达 → 完全无数据 */
    if (!af_ok && !has_vital) {
        out.event      = FUSION_DATA_QUALITY_LOW;
        out.confidence = 5;
        out.severity   = 0;
        snprintf(out.reason, sizeof(out.reason),
                 "无有效数据: 音频=%d 雷达=%d", af_ok, has_vital);
        goto emit;
    }

    /* 层2b: audio-only 模式 — 仅鼾声检测，不做呼吸暂停/低通气判断 */
    if (audio_only) {
        out.total_score = 70;  /* 音频有效基础分 */
        out.severity = 0;

        /* 累积 snore 秒数 */
        if (out.snoring) s_snore_sec_total++;

        /* 鼾声检测 — audio-only 简化版 */
        if (af_ok && s_af.model_valid && s_af.snore_prob >= 0.55f) {
            if (s_state != FUSION_SIMPLE_SNORE) {
                s_state = FUSION_SIMPLE_SNORE;
                s_state_start_ms = now;
            }
            out.event      = FUSION_SIMPLE_SNORE;
            out.confidence = (uint8_t)(s_af.snore_prob * 100.0f);
            if (out.confidence > 80) out.confidence = 80;
            snprintf(out.reason, sizeof(out.reason),
                     "单纯鼾声(audio-only): prob=%.2f type=%s",
                     s_af.snore_prob, snore_type_short_name(s_af.snore_type));
        } else {
            s_state = FUSION_NORMAL;
            s_state_start_ms = now;
            out.event      = FUSION_NORMAL;
            out.confidence = 30;
            snprintf(out.reason, sizeof(out.reason),
                     "audio-only 模式: 无雷达，仅监测鼾声");
        }

        /* audio-only 下不做呼吸暂停/低通气结论 */
        out.severity = 0;
        goto emit;
    }

    /* 层3: 基线未满 → 基线采集（不是预热） */
    if (baselining) {
        out.event = FUSION_WARMING_UP;
        out.confidence = 0;
        snprintf(out.reason, sizeof(out.reason),
                 "基线采集中 %d/120s", out.baseline_progress);
        goto emit;
    }

    /* BL:OK + 有生命体征 → 进入正常判断，不允许继续预热 */

    /* ── 累积 snore 秒数 ── */
    if (out.snoring) s_snore_sec_total++;

    /* ── 窗口特征 ── */
    window_features_t wf;
    compute_window_features(&wf, now);

    uint32_t state_elapsed = s_state_start_ms > 0 ? (now - s_state_start_ms) / 1000 : 0;

    /* ═══════════════════════════════════════════════════════
     * 硬性抑制条件检查
     * ═══════════════════════════════════════════════════════ */

    /* 体动事件使用“当前体动 + 连续计数 + 退出保持”。
     *
     * 旧逻辑使用 motion_max_10s，导致一次尖峰会让 body_movement_arousal
     * 在后续 10 秒内反复锁住；现在只用当前 motion 触发和退出。
     */
    static uint8_t s_high_motion_count = 0;
    static uint8_t s_quiet_motion_count = 0;
    static bool    s_arousal_latched = false;

    uint8_t cur_motion = _mt_fresh(now) ? s_rf.body_motion : 0;

    if (cur_motion > 50) {
        if (!s_arousal_latched) {
            s_arousal_latched = true;
            s_state = FUSION_BODY_MOVEMENT_AROUSAL;
            s_state_start_ms = now;
            s_arousal_total++;
        }
        s_high_motion_count = 0;
        s_quiet_motion_count = 0;
    } else if (cur_motion > 30) {
        if (s_high_motion_count < 3) s_high_motion_count++;
        s_quiet_motion_count = 0;
        if (s_high_motion_count >= 2 && !s_arousal_latched) {
            s_arousal_latched = true;
            s_state = FUSION_BODY_MOVEMENT_AROUSAL;
            s_state_start_ms = now;
            s_arousal_total++;
        }
    } else {
        s_high_motion_count = 0;
        if (cur_motion < 15) {
            if (s_quiet_motion_count < 10) s_quiet_motion_count++;
        } else {
            s_quiet_motion_count = 0;
        }
    }

    if (s_arousal_latched) {
        if (s_quiet_motion_count >= 5) {
            s_arousal_latched = false;
            s_state = FUSION_NORMAL;
            s_state_start_ms = now;
        } else {
            out.event      = FUSION_BODY_MOVEMENT_AROUSAL;
            out.confidence = (cur_motion > 50) ? 80 : ((cur_motion > 30) ? 70 : 60);
            out.severity   = 1;
            out.duration_sec = (uint16_t)state_elapsed;
            snprintf(out.reason, sizeof(out.reason),
                     "体动觉醒: current_motion=%u quiet=%u/5",
                     cur_motion, s_quiet_motion_count);
            goto emit;
        }
    }

    /* 体动 21~30：明显活动但非觉醒 → 体动干扰 */
    if (cur_motion > 20 && cur_motion <= 30) {
        s_state = FUSION_MOVEMENT_ARTIFACT;
        s_state_start_ms = now;
        out.event      = FUSION_MOVEMENT_ARTIFACT;
        out.confidence = 55;
        out.severity   = 0;
        out.duration_sec = 0;
        snprintf(out.reason, sizeof(out.reason),
                 "体动干扰: current_motion=%u, 暂停 apnea/hypopnea 判断", cur_motion);
        goto emit;
    }

    /* ═══════════════════════════════════════════════════════
     * 鼾声检测（simple_snore）
     * ═══════════════════════════════════════════════════════ */
    if (af_ok && s_af.model_valid && s_af.snore_prob >= 0.55f
        && out.breath_bpm >= 10.0f && out.breath_bpm <= 25.0f
        && out.breath_status == 1  /* 正常 */
        && wf.motion_max_10s <= 20.0f)
    {
        if (s_state != FUSION_SIMPLE_SNORE) {
            s_state = FUSION_SIMPLE_SNORE;
            s_state_start_ms = now;
        }
        out.event      = FUSION_SIMPLE_SNORE;
        out.confidence = (uint8_t)(s_af.snore_prob * 100.0f);
        out.severity   = 0;
        snprintf(out.reason, sizeof(out.reason),
                 "单纯鼾声(%s): prob=%.2f centroid=%.0fHz",
                 snore_type_short_name(s_af.snore_type),
                 s_af.snore_prob, s_af.spectral_centroid);
        goto emit;
    }

    /* ═══════════════════════════════════════════════════════
     * 呼吸暂停 / 低通气 检测
     * ═══════════════════════════════════════════════════════ */

    bool apnea_signal    = (wf.breath_amp_drop_ratio > 0.5f) && wf.breath_amp_current < 10.0f;
    bool hypopnea_signal = (wf.breath_amp_drop_ratio > 0.4f && wf.breath_amp_drop_ratio < 0.8f)
                           && wf.breath_amp_current > 3.0f;

    if (apnea_signal && wf.motion_max_10s <= 20.0f) {
        /* 分类：阻塞性 vs 中枢性 */
        bool is_obstructive = af_ok && s_af.model_valid
                              && (wf.snore_ratio_10s > 0.2f || s_af.airflow_sound_present);

        int conf = 0;
        char reason_buf[160];

        if (is_obstructive) {
            /* 阻塞性：有鼾声前驱 + 气流减弱 */
            conf = 40;
            if (wf.snore_ratio_30s > 0.3f)       conf += 15;
            if (wf.audio_energy_drop_ratio > 0.5f) conf += 15;
            if (wf.breath_amp_drop_ratio > 0.5f)   conf += 10;
            if (state_elapsed >= 10)               conf += 10;
            if (out.baseline_valid)                conf += 10;
            if (wf.audio_quality_score < 0.4f)     conf -= 15;
            if (wf.radar_quality_score < 0.4f)     conf -= 10;
            if (!out.baseline_valid)               conf -= 15;

            snprintf(reason_buf, sizeof(reason_buf),
                     "疑似阻塞性暂停: 鼾声比=%.2f 能量降=%.2f 呼吸降=%.2f 持续=%lus bl=%d",
                     wf.snore_ratio_30s, wf.audio_energy_drop_ratio,
                     wf.breath_amp_drop_ratio, state_elapsed, out.baseline_valid);

            if (conf >= 60 && state_elapsed >= 10) {
                out.event = FUSION_SUSPECTED_OBSTRUCTIVE;
                s_apnea_total++;
            } else if (conf >= 40 && state_elapsed >= 5) {
                out.event = FUSION_SUSPECTED_OBSTRUCTIVE;
            } else {
                out.event = FUSION_NORMAL;
            }
        } else {
            /* 中枢性：音频安静，呼吸努力减弱 */
            conf = 35;
            if (wf.snore_ratio_10s < 0.15f)        conf += 15;
            if (wf.breath_amp_drop_ratio > 0.7f)    conf += 15;
            if (wf.motion_max_10s < 15.0f)          conf += 10;
            if (state_elapsed >= 10)                conf += 10;
            if (out.baseline_valid)                 conf += 10;
            if (wf.radar_quality_score < 0.3f)      conf -= 15;
            if (!out.baseline_valid)                conf -= 20;

            snprintf(reason_buf, sizeof(reason_buf),
                     "疑似中枢性暂停: 安静=%.2f 呼吸降=%.2f 持续=%lus bl=%d",
                     wf.snore_ratio_10s, wf.breath_amp_drop_ratio, state_elapsed, out.baseline_valid);

            if (conf >= 55 && state_elapsed >= 10) {
                out.event = FUSION_SUSPECTED_CENTRAL;
                s_apnea_total++;
            } else if (conf >= 35 && state_elapsed >= 5) {
                out.event = FUSION_SUSPECTED_CENTRAL;
            } else {
                out.event = FUSION_NORMAL;
            }
        }

        if (conf < 0) conf = 0;
        if (conf > 100) conf = 100;
        out.confidence = (uint8_t)conf;
        out.duration_sec = (uint16_t)state_elapsed;
        snprintf(out.reason, sizeof(out.reason), "%s", reason_buf);

    } else if (hypopnea_signal && wf.motion_max_10s <= 20.0f) {
        int conf = 35;
        if (wf.breath_amp_drop_ratio > 0.5f)    conf += 15;
        if (state_elapsed >= 10)                conf += 15;
        if (wf.audio_energy_drop_ratio > 0.3f)  conf += 10;
        if (wf.recovery_breath_detected)        conf += 10;
        if (wf.motion_max_10s > 15.0f)          conf -= 10;
        if (!out.baseline_valid)                conf -= 15;
        if (conf < 0) conf = 0;
        if (conf > 100) conf = 100;

        out.event       = FUSION_SUSPECTED_HYPOPNEA;
        out.confidence  = (uint8_t)conf;
        out.duration_sec = (uint16_t)state_elapsed;
        s_hypopnea_total++;

        snprintf(out.reason, sizeof(out.reason),
                 "疑似低通气: 呼吸降=%.2f 持续=%lus 体动=%.0f bl=%d",
                 wf.breath_amp_drop_ratio, state_elapsed, wf.motion_max_10s, out.baseline_valid);

    } else {
        /* ── 恢复呼吸检测 ── */
        bool was_abnormal = (s_state == FUSION_SUSPECTED_OBSTRUCTIVE ||
                             s_state == FUSION_SUSPECTED_CENTRAL ||
                             s_state == FUSION_SUSPECTED_HYPOPNEA);

        if (was_abnormal && wf.recovery_breath_detected) {
            out.event      = FUSION_RECOVERY_BREATH;
            out.confidence = 70;
            s_recovery_total++;
            snprintf(out.reason, sizeof(out.reason), "异常后恢复呼吸");
        } else if (was_abnormal) {
            out.event      = FUSION_NORMAL;
            out.confidence = 25;
            snprintf(out.reason, sizeof(out.reason), "呼吸恢复正常");
        } else {
            out.event      = FUSION_NORMAL;
            out.confidence = 30;
            snprintf(out.reason, sizeof(out.reason), "正常呼吸");
        }

        s_state = FUSION_NORMAL;
        s_state_start_ms = now;
        out.duration_sec = 0;
    }

    /* 更新状态机 */
    if (out.event != FUSION_NORMAL && out.event != s_state) {
        s_state = out.event;
        s_state_start_ms = now;
    }

emit:
    /* ── 抑制低置信度 ── */
    if (!out.baseline_valid && out.confidence >= 50) {
        out.confidence = (uint8_t)(out.confidence * 0.6f);
        if (out.confidence < 60) {
            /* 降级为低置信，不计入事件但保留 reason */
        }
    }

    out.suspected_apnea_count  = s_apnea_total;
    out.suspected_hypopnea_count = s_hypopnea_total;
    out.recovery_breath_count  = s_recovery_total;
    out.movement_arousal_count = s_arousal_total;
    out.snore_sec_total        = s_snore_sec_total;

    /* ── 发布结果 ── */
    xQueueOverwrite(s_fusion_queue, &out);
    memcpy(&s_last_result, &out, sizeof(out));
    if (s_cb) s_cb(&out, s_user);

    /* ── FUSION 日志 — 每 5 秒或事件变化时输出 ── */
    {
        static uint32_t last_fusion_log = 0;
        static fusion_event_t last_logged_event = FUSION_NORMAL;
        bool event_changed = (out.event != last_logged_event
                              && out.event != FUSION_NORMAL);
        if (now - last_fusion_log >= 5000 || event_changed) {
            last_fusion_log = now;
            last_logged_event = out.event;
            ESP_LOGI(FUSION_TAG,
                "[FUSION] audio_valid=%d radar_valid=%d baseline_ready=%d "
                "event=%s score=%d risk=%d snore=%d",
                af_ok ? 1 : 0,
                has_vital ? 1 : 0,
                out.baseline_valid ? 1 : 0,
                sleep_fusion_event_label(out.event),
                out.total_score,
                out.severity,
                s_snore_sec_total);
        }
    }
}
