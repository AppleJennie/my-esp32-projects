/**
 * 电源管理：电池电压采集（VBAT 经 100k/100k 分压接 ADC）
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bsp_power_init(void);

/** 电池电压（伏），出错返回负值 */
float bsp_power_battery_voltage(void);

/** 估算电量百分比 0~100（按 3.3V~4.2V 线性估算，仅供参考） */
int bsp_power_battery_percent(void);

#ifdef __cplusplus
}
#endif
