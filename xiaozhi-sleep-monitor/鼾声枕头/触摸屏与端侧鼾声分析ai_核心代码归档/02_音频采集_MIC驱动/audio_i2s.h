#ifndef AUDIO_I2S_H
#define AUDIO_I2S_H
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "hal/gpio_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* DNESP32S3 板载 ES8388 — I2S 引脚（对齐 xiaozhi atk-dnesp32s3） */
#define AUDIO_I2S_NUM       I2S_NUM_0
#define AUDIO_I2S_MCLK      GPIO_NUM_3
#define AUDIO_I2S_BCLK      GPIO_NUM_46
#define AUDIO_I2S_WS        GPIO_NUM_9
#define AUDIO_I2S_DIN       GPIO_NUM_14   /* ESP32 RX ← ES8388 */
#define AUDIO_I2S_DOUT      GPIO_NUM_10   /* ESP32 TX → ES8388 */
#define AUDIO_SAMPLE_RATE   16000

/* ES8388 I2C */
#define ES8388_I2C_PORT     I2C_NUM_0
#define ES8388_I2C_SDA      GPIO_NUM_41
#define ES8388_I2C_SCL      GPIO_NUM_42
#define ES8388_I2C_ADDR     0x10

esp_err_t audio_i2s_init(void);
void audio_i2s_deinit(void);

/** 返回最近 1 秒 mono PCM（兼容接口） */
int  audio_i2s_get_1sec_pcm(int16_t *buf, int max_samples);

/**
 * 返回最近 samples 个连续 PCM 样本，时间顺序：buf[0]=最旧, buf[n-1]=最新。
 * 最大 64000 samples (4 秒)。
 */
int  audio_i2s_get_recent_pcm(int16_t *buf, int samples);

/** Ring buffer 诊断信息 */
typedef struct {
    int      write_pos;
    int      filled_samples;
    int      filled_ms;
    uint64_t total_written;
} audio_i2s_ring_info_t;

void audio_i2s_get_ring_info(audio_i2s_ring_info_t *out);

typedef struct {
    uint32_t samples_count;
    float    left_rms;
    float    right_rms;
    int16_t  peak;
    uint32_t zero_count;
    uint32_t clipped_count;
    bool     mic_ok;
    int      active_channel;
} audio_i2s_stats_t;

void audio_i2s_get_stats(audio_i2s_stats_t *out);
void audio_capture_task(void *pvParameters);

#ifdef __cplusplus
}
#endif
#endif
