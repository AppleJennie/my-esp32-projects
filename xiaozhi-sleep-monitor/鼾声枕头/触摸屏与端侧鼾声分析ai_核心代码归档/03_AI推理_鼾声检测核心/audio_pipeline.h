/**
 * audio_pipeline.h — 音频管线：PCM → 特征提取 → TFLite 推理 → 声学分类 → audio_feature_t
 *
 * 每秒运行一次：取 1 秒 PCM → extract features → snore_detector → snore_classifier
 */
#ifndef AUDIO_PIPELINE_H
#define AUDIO_PIPELINE_H
#include "fusion_types.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

/** Initialize pipeline: allocate PSRAM buffers, load TFLite model if enabled */
bool audio_pipeline_init(void);

/** Get latest audio feature snapshot (peek, non-blocking) */
bool audio_pipeline_get_feature(audio_feature_t *out);

/** Check if TFLite model is loaded and valid */
bool audio_pipeline_model_ok(void);

/** Get model status string: "OK" / "OFF" / "ERR" */
const char *audio_pipeline_model_status(void);

/** Audio pipeline task (main loop, 1 Hz) */
void audio_pipeline_task(void *pvParameters);

/** Get mic validity */
bool audio_pipeline_mic_ok(void);

#ifdef __cplusplus
}
#endif
#endif
