/**
 * snore_micro_feature_adapter.h
 *
 * 将 16kHz mono int16 PCM 转换成 microfrontend log-mel filterbank 特征。
 *
 * 输入：16kHz int16 PCM，完整 1 秒 16000 点
 * 输出：uint8 feature_buffer[1830] = 61 time × 30 freq
 * 每次推理前 reset 前端，确保特征无残留
 *
 * 基于 snore-recognition-main 的 micro_features_generator + feature_provider。
 */
#ifndef SNORE_MICRO_FEATURE_ADAPTER_H
#define SNORE_MICRO_FEATURE_ADAPTER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MICRO_FEATURE_FREQ_SLICES  30
#define MICRO_FEATURE_TIME_SLICES  61
#define MICRO_FEATURE_ELEMENT_COUNT (MICRO_FEATURE_FREQ_SLICES * MICRO_FEATURE_TIME_SLICES)  /* 1830 */
#define MICRO_AUDIO_SAMPLE_RATE    16000
#define MICRO_NEW_SAMPLES          7680   /* 480ms @ 16kHz */

/** 初始化 microfrontend 管线 */
bool snore_micro_feature_init(void);

/**
 * 将 PCM 数据送入 microfrontend，生成 uint8 特征。
 * @param pcm_samples  int16 PCM 数据
 * @param pcm_count    样本数（建议 7680 = 480ms）
 * @param features_out 输出特征，大小至少 MICRO_FEATURE_ELEMENT_COUNT
 * @return 成功返回 true
 */
bool snore_micro_generate_features(const int16_t *pcm_samples, size_t pcm_count,
                                    uint8_t *features_out);

#ifdef __cplusplus
}
#endif
#endif
