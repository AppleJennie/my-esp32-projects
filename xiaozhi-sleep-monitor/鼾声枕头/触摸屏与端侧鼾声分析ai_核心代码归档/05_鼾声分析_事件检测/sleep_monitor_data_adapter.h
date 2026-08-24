/**
 * sleep_monitor_data_adapter.h
 * 统一数据适配层: audio_feature + radar_feature + algorithm → SleepData_t
 */
#ifndef SLEEP_MONITOR_DATA_ADAPTER_H
#define SLEEP_MONITOR_DATA_ADAPTER_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize all subsystems: R60 UART, radar data, query task */
esp_err_t sleep_monitor_data_adapter_init(void);

/** Called every 500ms from LVGL main loop. Reads audio + radar → SleepData_t */
void sleep_monitor_data_adapter_update(void);

bool sleep_monitor_data_adapter_ready(void);

#ifdef __cplusplus
}
#endif
#endif
