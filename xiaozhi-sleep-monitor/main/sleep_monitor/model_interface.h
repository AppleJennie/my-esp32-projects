#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool model_interface_init(void);
bool model_interface_invoke(const float* input_features, float* output_scores);

#ifdef __cplusplus
}
#endif
