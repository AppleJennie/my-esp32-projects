/**
 * snore_audio_analyzer.c — 鼾声高级音频分析
 *
 * 功能: 频谱质心/低频比例/谐波比例/鼾声类型/气流线索/恢复呼吸线索
 * 约束: 不用 malloc, 不新增 >256B 数组, 复用 audio_processor FFT 结果
 * 注意: 所有输出均为声学线索, 非医学诊断
 */

#include "snore_audio_analyzer.h"
#include <string.h>
#include <math.h>
#include <stdbool.h>

/* ── 频段参数 ── */
#define LOW_FREQ_MIN_HZ     80.0f
#define LOW_FREQ_MAX_HZ     3000.0f
#define LF_RANGE_MIN_HZ      150.0f
#define LF_RANGE_MAX_HZ      600.0f
#define FUNDAMENTAL_MIN_HZ   80.0f
#define FUNDAMENTAL_MAX_HZ   300.0f
#define HARMONIC_WINDOW_HZ   30.0f
#define MAX_HARMONICS        4

/* ── 气流检测阈值 ── */
#define AIRFLOW_CENTROID_MIN_HZ  800
#define AIRFLOW_HARMONIC_MAX_X100 25
#define AIRFLOW_RMS_MIN           100

/* ── 恢复呼吸检测 ── */
#define RECOVERY_SILENCE_MS      6000
#define RECOVERY_RMS_MIN          200
#define RECOVERY_HOLD_MS          1500

/* ── 内部状态 ── */
static uint32_t s_last_silence_start_ms = 0;
static uint32_t s_recovery_onset_ms     = 0;

/* ═══════════════════════════════════════════════════════════════
 * 工具
 * ═══════════════════════════════════════════════════════════════ */

static int clamp_int(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static float clamp_float(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* ═══════════════════════════════════════════════════════════════
 * 重置
 * ═══════════════════════════════════════════════════════════════ */

void SnoreAudioAnalyzer_Reset(void)
{
    s_last_silence_start_ms = 0;
    s_recovery_onset_ms     = 0;
}

/* ═══════════════════════════════════════════════════════════════
 * 频谱质心
 * ═══════════════════════════════════════════════════════════════ */

/* FFT 输出是 dB 值 (-60~0), 用 fabs 近似幅度权重 */
static float compute_spectral_centroid(const float *mag, int bins,
                                        float bin_hz, float *out_total_power)
{
    float total = 1e-6f, weighted = 0.0f;
    for (int i = 0; i < bins; i++) {
        float freq = (float)i * bin_hz;
        if (freq < LOW_FREQ_MIN_HZ || freq > LOW_FREQ_MAX_HZ) continue;
        float p = fabsf(mag[i]);  /* dB绝对值近似权重, 避免平方反转 */
        total    += p;
        weighted += p * freq;
    }
    if (out_total_power) *out_total_power = total;
    return total > 1e-6f ? weighted / total : 0.0f;
}

/* ═══════════════════════════════════════════════════════════════
 * 低频比例 (150~600Hz / 80~3000Hz) ×100
 * ═══════════════════════════════════════════════════════════════ */

static uint8_t compute_low_freq_ratio(const float *mag, int bins, float bin_hz)
{
    float total = 1e-6f, low = 0.0f;
    for (int i = 0; i < bins; i++) {
        float freq = (float)i * bin_hz;
        float p = fabsf(mag[i]);  /* dB绝对值 */
        if (freq >= LOW_FREQ_MIN_HZ && freq <= LOW_FREQ_MAX_HZ)
            total += p;
        if (freq >= LF_RANGE_MIN_HZ && freq <= LF_RANGE_MAX_HZ)
            low += p;
    }
    if (total < 1e-6f) return 0;
    int ratio = (int)(low * 100.0f / total);
    return (uint8_t)clamp_int(ratio, 0, 100);
}

/* ═══════════════════════════════════════════════════════════════
 * 谐波比例
 * 在 80~300Hz 找基频峰值 f0, 统计 f0/2f0/3f0/4f0 窗口内能量
 * ═══════════════════════════════════════════════════════════════ */

static uint8_t compute_harmonic_ratio(const float *mag, int bins, float bin_hz)
{
    /* 找基频峰值 */
    int f0_bin = -1;
    float f0_max = -1.0f;
    for (int i = 0; i < bins; i++) {
        float freq = (float)i * bin_hz;
        if (freq < FUNDAMENTAL_MIN_HZ || freq > FUNDAMENTAL_MAX_HZ) continue;
        if (mag[i] > f0_max) { f0_max = mag[i]; f0_bin = i; }
    }
    if (f0_bin < 1 || f0_max < 0.01f) return 0;

    float f0_hz = (float)f0_bin * bin_hz;
    float total = 1e-6f, harmonic = 0.0f;

    for (int i = 0; i < bins; i++) {
        float freq = (float)i * bin_hz;
        if (freq < LOW_FREQ_MIN_HZ || freq > LOW_FREQ_MAX_HZ) continue;
        total += fabsf(mag[i]);  /* dB绝对值 */
    }

    for (int k = 1; k <= MAX_HARMONICS; k++) {
        float target = f0_hz * (float)k;
        float lo = target - HARMONIC_WINDOW_HZ;
        float hi = target + HARMONIC_WINDOW_HZ;
        for (int i = 0; i < bins; i++) {
            float freq = (float)i * bin_hz;
            if (freq >= lo && freq <= hi) {
                harmonic += fabsf(mag[i]);  /* dB绝对值 */
                break;
            }
        }
    }

    if (total < 1e-6f) return 0;
    int ratio = (int)(harmonic * 100.0f / total);
    return (uint8_t)clamp_int(ratio, 0, 100);
}

/* ═══════════════════════════════════════════════════════════════
 * 鼾声类型判断 v2 (收紧阈值, 减少环境噪声误识别)
 * ═══════════════════════════════════════════════════════════════ */

static uint8_t classify_snore_type(uint16_t centroid_hz,
                                    uint8_t low_freq_x100,
                                    uint8_t harmonic_x100,
                                    uint8_t rms,
                                    uint8_t snore_score)
{
    /* 门控: 能量太低/频谱无效 → 不分类 */
    if (rms < 80 || centroid_hz < 50 || centroid_hz > 4000)
        return SNORE_TYPE_UNKNOWN;
    if (snore_score < 100 && rms < 200)
        return SNORE_TYPE_UNKNOWN;

    int sc_throat = 0, sc_nasal = 0, sc_mouth = 0;

    /* 喉鼾: 频谱中心 150~600Hz 且 低频占比>45 (两个条件都满足) */
    if (centroid_hz >= 150 && centroid_hz <= 600)
        sc_throat++;
    if (low_freq_x100 > 45)
        sc_throat++;
    if (centroid_hz <= 400 && low_freq_x100 > 50)
        sc_throat++;  /* bonus */

    /* 鼻鼾: 频谱中心 500~1800Hz + 谐波比>35 */
    if (centroid_hz >= 500 && centroid_hz <= 1800)
        sc_nasal++;
    if (harmonic_x100 > 35)
        sc_nasal++;
    if (harmonic_x100 > 40 && centroid_hz >= 600 && centroid_hz <= 1500)
        sc_nasal++;  /* bonus */

    /* 口呼吸鼾: 频谱中心>1500Hz + 谐波比<25 + ZCR偏高 */
    if (centroid_hz > 1500)
        sc_mouth++;
    if (harmonic_x100 < 25)
        sc_mouth++;
    if (centroid_hz > 2000 && harmonic_x100 < 20)
        sc_mouth++;  /* bonus */

    /* 门槛: 至少 2 分才认定 */
    if (sc_throat < 2 && sc_nasal < 2 && sc_mouth < 2) {
        if (snore_score >= 180 && rms > 250)
            return SNORE_TYPE_MIXED;
        return SNORE_TYPE_NONE;  /* 不满足条件直接NO */
    }

    if (sc_nasal >= sc_throat && sc_nasal >= sc_mouth && sc_nasal >= 2)
        return SNORE_TYPE_NASAL;
    if (sc_throat >= sc_nasal && sc_throat >= sc_mouth && sc_throat >= 2)
        return SNORE_TYPE_THROAT;
    if (sc_mouth >= sc_throat && sc_mouth >= sc_nasal && sc_mouth >= 2)
        return SNORE_TYPE_MOUTH;
    return SNORE_TYPE_NONE;  /* fallback: 低于门槛 */
}

/* ═══════════════════════════════════════════════════════════════
 * 类型置信度估算 v2
 * ═══════════════════════════════════════════════════════════════ */

static uint8_t estimate_confidence(uint8_t snore_type, uint8_t snore_score,
                                    uint8_t quality, uint16_t rms)
{
    if (snore_type <= SNORE_TYPE_NONE)  return 0;
    if (snore_type == SNORE_TYPE_UNKNOWN) return 0;
    if (snore_type == SNORE_TYPE_MIXED)
        return (uint8_t)clamp_int((int)snore_score / 4, 15, 60);

    int conf = 25;  /* 基础置信 */
    conf += (int)snore_score / 5;  /* score贡献 */
    if (rms > 400)  conf += 10;
    if (rms > 800)  conf += 10;
    if (rms < 150)  conf -= 15;   /* 低能量惩罚 */
    if (quality > 0) conf -= 10;  /* 质量差惩罚 */
    return (uint8_t)clamp_int(conf, 0, 100);
}

/* ═══════════════════════════════════════════════════════════════
 * 气流声检测 (声学线索, 非医学诊断)
 * 保守规则: centroid>800, harmonic<25%, zcr偏高, rms高于静音
 * ═══════════════════════════════════════════════════════════════ */

static uint8_t detect_airflow(uint16_t centroid_hz, uint8_t harmonic_x100,
                               uint8_t zcr_x100, uint16_t rms)
{
    if (centroid_hz > (uint16_t)AIRFLOW_CENTROID_MIN_HZ
        && harmonic_x100 < AIRFLOW_HARMONIC_MAX_X100
        && zcr_x100 > 15
        && rms > AIRFLOW_RMS_MIN)
        return 1;
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 * 恢复呼吸音检测 (声学线索, 非医学诊断, 不是呼吸暂停判断)
 * 连续 >6秒 静默/低能量 → 突然出现高RMS鼾声/喘息 → 标记 1.5秒
 * ═══════════════════════════════════════════════════════════════ */

static uint8_t detect_recovery_breath(uint8_t snore_active, uint16_t rms,
                                       uint8_t snore_score, uint32_t now_ms)
{
    bool is_low = (rms < 50) && (snore_active == 0);

    if (is_low) {
        if (s_last_silence_start_ms == 0)
            s_last_silence_start_ms = now_ms;
    } else {
        s_last_silence_start_ms = 0;
    }

    /* 检查是否在恢复呼吸保持期内 */
    if (s_recovery_onset_ms > 0) {
        if (now_ms - s_recovery_onset_ms < RECOVERY_HOLD_MS)
            return 1;
        s_recovery_onset_ms = 0;
    }

    /* 检测: 之前沉默了 >6秒, 现在突然高能量 */
    if (s_last_silence_start_ms > 0
        && (now_ms - s_last_silence_start_ms) >= RECOVERY_SILENCE_MS
        && rms >= RECOVERY_RMS_MIN
        && snore_score > 120) {
        s_recovery_onset_ms = now_ms;
        s_last_silence_start_ms = 0;
        return 1;
    }

    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 * 主入口
 * ═══════════════════════════════════════════════════════════════ */

void SnoreAudioAnalyzer_Update(
    const float *fft_mag, int fft_bins, float bin_hz,
    uint16_t rms, uint16_t peak, uint8_t zcr_x100,
    uint8_t snore_active, uint8_t snore_score,
    uint8_t audio_valid, uint8_t quality, uint32_t now_ms,
    snore_advanced_audio_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));

    /* ── 音频无效时清零 ── */
    if (!audio_valid) {
        out->snore_type = SNORE_TYPE_UNKNOWN;
        return;
    }

    /* ── 无鼾声时: 频谱照算(供主板参考), 类型标 NONE ── */
    /* 先算频谱指标, 无论是否有鼾声 */
    if (fft_mag && fft_bins > 0) {
        float total_pwr = 0;
        float cent = compute_spectral_centroid(fft_mag, fft_bins, bin_hz, &total_pwr);
        out->spectral_centroid_hz = (uint16_t)clamp_int((int)cent, 0, 8000);
        out->low_freq_ratio_x100  = compute_low_freq_ratio(fft_mag, fft_bins, bin_hz);
        out->harmonic_ratio_x100  = compute_harmonic_ratio(fft_mag, fft_bins, bin_hz);
    }

    if (!snore_active || rms < 80 || snore_score < 80) {
        /* 信号质量不足以做分类 */
        out->snore_type = SNORE_TYPE_NONE;
        out->type_confidence = 0;
        out->airflow_sound_present = detect_airflow(
            out->spectral_centroid_hz, out->harmonic_ratio_x100, zcr_x100, rms);
        out->recovery_breath_sound = detect_recovery_breath(
            snore_active, rms, snore_score, now_ms);
        return;
    }

    /* ── 信号有效: 完整分析 ── */
    out->snore_type = classify_snore_type(
        out->spectral_centroid_hz,
        out->low_freq_ratio_x100,
        out->harmonic_ratio_x100,
        (uint8_t)clamp_int((int)rms, 0, 255),
        snore_score);

    out->type_confidence = estimate_confidence(
        out->snore_type, snore_score, quality, rms);

    out->airflow_sound_present = detect_airflow(
        out->spectral_centroid_hz, out->harmonic_ratio_x100, zcr_x100, rms);

    out->recovery_breath_sound = detect_recovery_breath(
        snore_active, rms, snore_score, now_ms);

    (void)peak;  /* unused for now, kept for future */
}

/* ═══════════════════════════════════════════════════════════════
 * 中文标签
 * ═══════════════════════════════════════════════════════════════ */

const char *snore_type_cn_label(snore_type_t t)
{
    switch (t) {
        case SNORE_TYPE_NASAL:   return "鼻鼾";
        case SNORE_TYPE_THROAT:  return "喉鼾";
        case SNORE_TYPE_MOUTH:   return "口呼吸鼾";
        case SNORE_TYPE_MIXED:   return "混合型鼾";
        case SNORE_TYPE_UNKNOWN: return "未知";
        default:                 return "无鼾声";
    }
}
