/**
 * snore_audio_analyzer.h — 鼾声高级音频分析
 *
 * 从 FFT 频谱计算: 频谱质心/低频比例/谐波比例/鼾声类型/气流线索/恢复呼吸线索
 * 本模块只做声学分析,不做医学诊断。不使用 malloc,不新增大数组。
 */
#pragma once

#include <stdint.h>
#include "snore_classifier.h"   /* 共用 snore_type_t 枚举 */

#ifdef __cplusplus
extern "C" {
#endif

/* ── 高级音频分析输出 ── */
typedef struct {
    uint8_t  snore_type;              /* snore_type_t: 0~5 */
    uint8_t  type_confidence;         /* 0~100 */
    uint16_t spectral_centroid_hz;    /* 频谱质心 Hz */
    uint8_t  low_freq_ratio_x100;     /* 低频能量比例 0~100 */
    uint8_t  harmonic_ratio_x100;     /* 谐波比例 0~100 */
    uint8_t  airflow_sound_present;   /* 声学气流线索,非医学诊断 */
    uint8_t  recovery_breath_sound;   /* 声学恢复呼吸线索,非医学诊断 */
} snore_advanced_audio_t;

/* ── 重置内部状态 ── */
void SnoreAudioAnalyzer_Reset(void);

/**
 * @brief 更新高级音频分析
 * @param fft_mag       FFT幅值数组 (来自 audio_processor, 65 bins)
 * @param fft_bins      FFT bin 数量 (SNORE_FFT_BINS = 65)
 * @param bin_hz        每个 bin 的带宽 Hz (16000/256*2 ≈ 125)
 * @param rms           RMS 能量
 * @param peak          峰值
 * @param zcr_x100      过零率 ×100
 * @param snore_active  TFLite模型判定是否鼾声活跃
 * @param snore_score   TFLite 模型得分
 * @param audio_valid   音频数据是否有效
 * @param quality       数据质量 0=OK
 * @param now_ms        当前时间戳 ms
 * @param out           输出结构体
 */
void SnoreAudioAnalyzer_Update(
    const float *fft_mag,
    int fft_bins,
    float bin_hz,
    uint16_t rms,
    uint16_t peak,
    uint8_t zcr_x100,
    uint8_t snore_active,
    uint8_t snore_score,
    uint8_t audio_valid,
    uint8_t quality,
    uint32_t now_ms,
    snore_advanced_audio_t *out
);

/* ── 获取鼾声类型中文标签 ── */
const char *snore_type_cn_label(snore_type_t t);

#ifdef __cplusplus
}
#endif
