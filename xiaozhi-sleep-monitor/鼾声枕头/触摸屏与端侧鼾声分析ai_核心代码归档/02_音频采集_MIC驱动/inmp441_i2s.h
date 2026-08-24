/**
 * inmp441_i2s.h — INMP441 MEMS 麦克风 I2S 驱动 v3 (32-bit slot, raw32 诊断)
 *
 * 硬件：INMP441 外接模块
 *       BCLK=12, WS=11, DIN=13, 不需要MCLK
 *       16000Hz, 32bit data / 32bit slot, mono, Philips I2S
 *
 * INMP441 是 24-bit 设备，I2S 帧内实际占高 24 位（左对齐到 32-bit slot）。
 * 我们以 32-bit 接收，再将 raw32 右移得到 16-bit PCM。
 *
 * 快速切换：
 *   INMP441_USE_LEFT_SLOT = 1 (左) / 0 (右)
 *   INMP441_PCM_SHIFT     = 8 / 10 / 12 / 14 / 16  测试用
 */

#ifndef INMP441_I2S_H
#define INMP441_I2S_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "hal/gpio_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── I2S 硬件引脚 ── */
#define INMP441_I2S_NUM         I2S_NUM_1
#define INMP441_I2S_BCLK        GPIO_NUM_12
#define INMP441_I2S_WS          GPIO_NUM_11
#define INMP441_I2S_DIN         GPIO_NUM_13
#define INMP441_SAMPLE_RATE     16000

/* ── I2S 位宽配置 ── */
#define INMP441_DATA_BIT_WIDTH  I2S_DATA_BIT_WIDTH_32BIT
#define INMP441_SLOT_BIT_WIDTH  I2S_SLOT_BIT_WIDTH_AUTO

/* ── Slot 切换 ── */
#define INMP441_USE_LEFT_SLOT   1       /* 1=LEFT, 0=RIGHT */

/* ── PCM 右移位数 ── */
#define INMP441_PCM_SHIFT       14      /* 8/10/12/14/16 */

/* ── 诊断开关 ── */
#define CONFIG_INMP441_DEBUG_RAW        1
#define INMP441_DIAG_MULTI_SHIFT        1   /* 同时打印 shift=8/14/16 的 rms/peak */

/* ── Ring Buffer ── */
#define INMP441_RING_SECONDS    4
#define INMP441_RING_SAMPLES    (INMP441_SAMPLE_RATE * INMP441_RING_SECONDS)  /* 64000 */

/* ── Capture 块大小（32-bit words）── */
#define INMP441_CAPTURE_BLOCK   256

esp_err_t inmp441_i2s_init(void);
esp_err_t inmp441_audio_start(void);
size_t    inmp441_read_pcm(int16_t *out, size_t samples, uint32_t timeout_ms);
size_t    audio_input_read_pcm(int16_t *out, size_t samples, uint32_t timeout_ms);
void      inmp441_get_stats(float *rms_out, int16_t *peak_out, uint32_t *samples_out);
bool      inmp441_mic_ok(void);
void      inmp441_capture_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif
