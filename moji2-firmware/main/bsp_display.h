/**
 * 屏幕驱动：ST77916 QSPI (360x360) + 背光 PWM
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 QSPI 总线、ST77916 面板和背光，点亮屏幕 */
esp_err_t bsp_display_init(void);

/**
 * 把一整行带宽度的像素刷到屏幕 (x: 0 ~ LCD_H_RES)
 * data 为 RGB565 缓冲区，共 LCD_H_RES * (y1 - y0) 个像素。
 * SPI 面板要求高字节先发，缓冲区需为大端序（见 emoji.c 的 PX 宏）。
 */
void bsp_display_flush(const void *data, int y0, int y1);

/** 背光亮度，0 ~ 100 (%) */
void bsp_display_backlight_set(uint8_t percent);

#ifdef __cplusplus
}
#endif
