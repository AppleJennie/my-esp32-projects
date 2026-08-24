/**
 * @file    sleep_audio_adapter.h
 * @brief   音频适配层：INMP441 → audio_pipeline → SleepData_t
 */

#ifndef SLEEP_AUDIO_ADAPTER_H
#define SLEEP_AUDIO_ADAPTER_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t sleep_audio_adapter_init(void);
void sleep_audio_adapter_update(void);
bool sleep_audio_adapter_ready(void);

#ifdef __cplusplus
}
#endif

#endif
