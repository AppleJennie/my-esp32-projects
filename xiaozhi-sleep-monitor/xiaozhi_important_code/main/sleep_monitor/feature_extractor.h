#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool feature_extractor_init(void);
void extract_audio_features(const int16_t* audio_samples, int sample_count, float* features);
void extract_radar_features(const float* radar_samples, int sample_count, float* features);
void extract_physio_features(const float* physio_samples, int sample_count, float* features);

#ifdef __cplusplus
}
#endif
