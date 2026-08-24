/**
 * RGB 状态灯：板载 TX1812（WS2812 协议兼容），单颗
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bsp_led_init(void);

/** 设置颜色（0~255），传 0,0,0 即熄灭 */
void bsp_led_set(uint8_t r, uint8_t g, uint8_t b);

#ifdef __cplusplus
}
#endif
