/**
 * snore_detector_micro.h
 *
 * 使用 snore-recognition-main 的 micro model（1830 uint8 input, 2-class output）。
 */
#ifndef SNORE_DETECTOR_MICRO_H
#define SNORE_DETECTOR_MICRO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct snore_detector_micro_t snore_detector_micro_t;

/** 结果 */
typedef struct {
    uint8_t snore_score;       /* output[0] raw uint8 */
    uint8_t background_score;  /* output[1] raw uint8 */
    float   snore_prob;        /* snore_score / 255.0 */
    bool    is_snoring;        /* avg3 > 128 */
    uint32_t inference_time_ms;
} snore_micro_result_t;

/** 初始化（arena 默认 100KB PSRAM） */
snore_detector_micro_t *snore_detector_micro_init(
    const uint8_t *model_data, uint32_t model_size, uint32_t arena_size);

/** 运行推理。input_features 必须为 MICRO_FEATURE_ELEMENT_COUNT (1830) uint8 */
int snore_detector_micro_detect(snore_detector_micro_t *det,
                                 const uint8_t *input_features,
                                 snore_micro_result_t *result);

void snore_detector_micro_deinit(snore_detector_micro_t *det);
const char *snore_detector_micro_err(snore_detector_micro_t *det);

#ifdef __cplusplus
}
#endif
#endif
