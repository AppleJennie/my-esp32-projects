/**
 * radar_wave_analyzer.c — 雷达呼吸/心率波形深度分析
 *
 * 5Hz 采样，60 秒窗口 (300 点)
 * 提取：breath_cv, breath_pause_sec, breath_rate_wave,
 *       hr_sdnn_proxy, heart_rate_wave, quality flags
 */
#include "radar_wave_analyzer.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "WAVE_ANALYZER";

/* ═══════════════════════════════════════════════════════════════
 * 滑动窗口均值/标准差
 * ═══════════════════════════════════════════════════════════════ */

static float compute_mean(const float *arr, int n)
{
    if (n <= 0) return 0;
    float sum = 0;
    for (int i = 0; i < n; i++) sum += arr[i];
    return sum / (float)n;
}

static float compute_std(const float *arr, int n, float mean)
{
    if (n <= 1) return 0;
    float var = 0;
    for (int i = 0; i < n; i++) {
        float d = arr[i] - mean;
        var += d * d;
    }
    return sqrtf(var / (float)n);
}

/* ═══════════════════════════════════════════════════════════════
 * 从 ring buffer 拷贝到线性数组
 * ═══════════════════════════════════════════════════════════════ */

static int ring_to_linear(const uint8_t *buf, uint16_t idx,
                           uint16_t cnt, uint16_t buf_size,
                           float *out, int max_out)
{
    if (cnt == 0) return 0;
    int start = (int)idx - (int)cnt;
    if (start < 0) start += buf_size;
    start %= buf_size;

    int n = (cnt < max_out) ? (int)cnt : max_out;
    for (int i = 0; i < n; i++) {
        out[i] = (float)buf[(start + i) % buf_size];
    }
    return n;
}

/* ═══════════════════════════════════════════════════════════════
 * 呼吸波形分析：找峰值，算周期，算 CV
 * ═══════════════════════════════════════════════════════════════ */

#define MAX_CYCLES  40   /* 60 秒窗口最多约 33 个呼吸周期 (60/1.8) */

static int find_breath_cycles(const float *wave, int n,
                               float periods[MAX_CYCLES])
{
    if (n < 10) return 0;

    /* 1. 去直流 + 轻度平滑 */
    float mean = compute_mean(wave, n);
    float smoothed[WAVE_BUF_LEN];
    for (int i = 0; i < n; i++) {
        smoothed[i] = wave[i] - mean;
    }

    /* 2. 找局部极大值（波峰）—— 3 点简单检测 */
    int peak_indices[MAX_CYCLES + 1];
    int peak_cnt = 0;
    for (int i = 1; i < n - 1 && peak_cnt < MAX_CYCLES + 1; i++) {
        if (smoothed[i] > smoothed[i-1] && smoothed[i] >= smoothed[i+1]
            && smoothed[i] > 0) {  /* 只取高于均值的峰 */
            peak_indices[peak_cnt++] = i;
        }
    }

    if (peak_cnt < 3) return 0;  /* 至少 3 个周期 */

    /* 3. 算峰峰间距 → 周期（秒） */
    int cycle_cnt = 0;
    const float dt = 1.0f / (float)WAVE_FS;  /* 0.2s */

    for (int i = 1; i < peak_cnt && cycle_cnt < MAX_CYCLES; i++) {
        float period = (float)(peak_indices[i] - peak_indices[i-1]) * dt;

        /* 生理合理性过滤 */
        if (period >= BREATH_PERIOD_MIN_SEC && period <= BREATH_PERIOD_MAX_SEC) {
            periods[cycle_cnt++] = period;
        }
    }

    return cycle_cnt;
}

static float detect_breath_pause(const float *wave, int n,
                                  float baseline_amp, float *amp_out)
{
    if (n < 10 || baseline_amp < 1.0f) {
        if (amp_out) *amp_out = 0;
        return 0;
    }

    /* 滑窗找 min/max */
    float mn = 255, mx = 0;
    int recent_n = n < (WAVE_FS * 12) ? n : (WAVE_FS * 12);  /* 最近 12 秒 */
    int start = n - recent_n;
    for (int i = start; i < n; i++) {
        if (wave[i] < mn) mn = wave[i];
        if (wave[i] > mx) mx = wave[i];
    }
    float amp = mx - mn;
    if (amp_out) *amp_out = amp;

    if (baseline_amp < 1.0f) return 0;

    /* 连续低幅检测：从末尾向前扫 */
    float threshold = baseline_amp * BREATH_PAUSE_DROP_RATIO;
    int low_count = 0;
    for (int i = n - 1; i >= 0; i--) {
        /* 用局部幅度判断：最近几个点的范围 */
        if (i > 5) {
            float local_mn = 255, local_mx = 0;
            for (int j = i - 5; j <= i; j++) {
                if (wave[j] < local_mn) local_mn = wave[j];
                if (wave[j] > local_mx) local_mx = wave[j];
            }
            if ((local_mx - local_mn) < threshold) {
                low_count++;
            } else {
                break;
            }
        } else {
            low_count++;
        }
    }

    return (float)low_count / (float)WAVE_FS;  /* 转秒 */
}

/* ═══════════════════════════════════════════════════════════════
 * 心率波形分析：找峰，算 IBI，算 SDNN
 * ═══════════════════════════════════════════════════════════════ */

#define MAX_BEATS   100  /* 60 秒窗口最多 133 跳 (60/0.45) */

static int find_heart_beats(const float *wave, int n,
                             float ibi[MAX_BEATS])
{
    if (n < 15) return 0;

    /* 去直流 */
    float mean = compute_mean(wave, n);
    float centered[WAVE_BUF_LEN];
    for (int i = 0; i < n; i++) centered[i] = wave[i] - mean;

    /* 简单 3 点峰值检测 */
    int peak_idx[MAX_BEATS + 1];
    int peak_cnt = 0;
    for (int i = 1; i < n - 1 && peak_cnt < MAX_BEATS + 1; i++) {
        if (centered[i] > centered[i-1] && centered[i] >= centered[i+1]
            && centered[i] > 3.0f) {  /* 最小幅度 3 */
            peak_idx[peak_cnt++] = i;
        }
    }

    if (peak_cnt < 5) return 0;

    /* IBI → ms */
    const float dt_ms = 1000.0f / (float)WAVE_FS;  /* 200ms */
    int beat_cnt = 0;
    for (int i = 1; i < peak_cnt && beat_cnt < MAX_BEATS; i++) {
        float ibi_ms = (float)(peak_idx[i] - peak_idx[i-1]) * dt_ms;
        float ibi_sec = ibi_ms / 1000.0f;
        if (ibi_sec >= HEART_IBI_MIN_SEC && ibi_sec <= HEART_IBI_MAX_SEC) {
            ibi[beat_cnt++] = ibi_ms;
        }
    }
    return beat_cnt;
}

/* ═══════════════════════════════════════════════════════════════
 * 初始化
 * ═══════════════════════════════════════════════════════════════ */

void radar_wave_analyzer_init(void)
{
    ESP_LOGI(TAG, "Wave analyzer init, fs=%dHz window=%ds buf=%d",
             WAVE_FS, WAVE_BUF_SEC, WAVE_BUF_LEN);
}

/* ═══════════════════════════════════════════════════════════════
 * 主分析函数
 * ═══════════════════════════════════════════════════════════════ */

void radar_wave_analyze(const sleep_radar_data_t *radar,
                         RadarWaveFeatures *out,
                         uint32_t now_ms,
                         float bl_amp)
{
    memset(out, 0, sizeof(RadarWaveFeatures));
    if (!radar) return;

    /* ── 体动伪影检测 ── */
    uint32_t age_motion = now_ms - radar->last_body_motion_ms;
    bool motion_fresh = (age_motion <= 3000);
    uint8_t cur_motion = motion_fresh ? radar->body_motion : 0;
    out->motion_artifact = (cur_motion > MOTION_ARTIFACT_THRESH);

    /* ═══════════════════════════════════════════════════════════
     * 呼吸波形分析
     * ═══════════════════════════════════════════════════════════ */

    bool breath_fresh = ((now_ms - radar->last_breath_wave_ms) <= 2000);
    uint16_t breath_cnt = radar->breath_wave_count;

    if (breath_fresh && breath_cnt >= 20) {
        /* 拷贝到线性缓冲 */
        float wave[WAVE_BUF_LEN];
        int n = ring_to_linear(radar->breath_wave_buf,
                                radar->breath_wave_idx,
                                breath_cnt, BREATH_WAVE_BUF_SIZE,
                                wave, WAVE_BUF_LEN);

        /* 幅度 */
        float mn = 255, mx = 0;
        for (int i = 0; i < n; i++) {
            if (wave[i] < mn) mn = wave[i];
            if (wave[i] > mx) mx = wave[i];
        }
        out->breath_amp = mx - mn;

        /* 基线 */
        float baseline = (bl_amp > 1.0f) ? bl_amp : out->breath_amp;
        out->breath_amp_baseline = baseline;

        /* 呼吸周期分析 */
        float periods[MAX_CYCLES];
        int cycle_cnt = find_breath_cycles(wave, n, periods);

        if (cycle_cnt >= 3) {
            float period_mean = compute_mean(periods, cycle_cnt);
            float period_std  = compute_std(periods, cycle_cnt, period_mean);

            out->breath_cycle_valid = true;
            out->breath_cycle_count = cycle_cnt;
            if (period_mean > 0.5f) {
                out->breath_rate_wave = 60.0f / period_mean;
                out->breath_cv = period_std / period_mean;
            }
        }

        /* 暂停检测 */
        out->breath_pause_sec = detect_breath_pause(wave, n, baseline, NULL);

        /* 波形质量 */
        float quality = 1.0f;
        if (out->breath_amp < 5.0f) quality *= 0.3f;
        else if (out->breath_amp < 10.0f) quality *= 0.6f;
        if (out->motion_artifact) quality *= 0.3f;
        if (!out->breath_cycle_valid) quality *= 0.5f;
        if (out->breath_pause_sec > 20.0f) quality *= 0.4f;  /* 长时间无呼吸 */
        out->breath_quality = quality;
    }

    /* ═══════════════════════════════════════════════════════════
     * 心率波形分析
     * ═══════════════════════════════════════════════════════════ */

    bool heart_fresh = ((now_ms - radar->last_heart_wave_ms) <= 2000);
    uint16_t heart_cnt = radar->heart_wave_count;

    if (heart_fresh && heart_cnt >= 20) {
        float wave[WAVE_BUF_LEN];
        int n = ring_to_linear(radar->heart_wave_buf,
                                radar->heart_wave_idx,
                                heart_cnt, HEART_WAVE_BUF_SIZE,
                                wave, WAVE_BUF_LEN);

        /* 幅度 */
        float mn = 255, mx = 0;
        for (int i = 0; i < n; i++) {
            if (wave[i] < mn) mn = wave[i];
            if (wave[i] > mx) mx = wave[i];
        }
        out->heart_amp = mx - mn;

        /* IBI → SDNN */
        float ibi[MAX_BEATS];
        int beat_cnt = find_heart_beats(wave, n, ibi);

        if (beat_cnt >= 8) {
            float ibi_mean = compute_mean(ibi, beat_cnt);
            float ibi_std  = compute_std(ibi, beat_cnt, ibi_mean);

            out->heart_cycle_valid = true;
            out->heart_beat_count = beat_cnt;
            out->heart_rate_wave = (ibi_mean > 0) ? 60000.0f / ibi_mean : 0;
            out->hr_sdnn_ms = ibi_std;
        }

        /* 心率波形质量 */
        float hq = 1.0f;
        if (out->heart_amp < 3.0f) hq *= 0.3f;
        if (out->motion_artifact) hq *= 0.2f;
        if (!out->heart_cycle_valid) hq *= 0.4f;

        /* 交叉验证：波形反推心率 vs 雷达上报心率 */
        if (out->heart_cycle_valid && radar->heart_rate > 0) {
            float diff = fabsf(out->heart_rate_wave - (float)radar->heart_rate);
            if (diff > 20.0f) hq *= 0.4f;
            else if (diff > 10.0f) hq *= 0.7f;
        }
        out->hrv_quality = hq;
    }

    /* ── 综合有效标记 ── */
    out->valid = (out->breath_quality >= WAVE_QUALITY_MIN ||
                  out->hrv_quality >= WAVE_QUALITY_MIN);
}

/* ═══════════════════════════════════════════════════════════════
 * 轻量版：仅呼吸
 * ═══════════════════════════════════════════════════════════════ */

void radar_wave_analyze_breath_only(const sleep_radar_data_t *radar,
                                     RadarWaveFeatures *out,
                                     uint32_t now_ms,
                                     float bl_amp)
{
    radar_wave_analyze(radar, out, now_ms, bl_amp);
    /* 心率相关字段已为 0，不影响 */
}
