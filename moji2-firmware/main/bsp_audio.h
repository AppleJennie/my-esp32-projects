/**
 * 音频驱动：ES8311 Codec (I2C 配置) + I2S 收发声道 + D 类功放使能
 *
 * 提供：
 *  - 提示音播放（喇叭通路：I2S DOUT -> ES8311 DAC -> 功放 -> 喇叭）
 *  - 麦克风采样接口（麦克风 -> ES8311 ADC -> I2S DIN），供后续语音链路使用
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 I2C、ES8311 寄存器和 I2S 双工声道 */
esp_err_t bsp_audio_init(void);

/** 使能/关闭 D 类功放（播放前打开，平时关闭省电） */
void bsp_audio_pa_enable(bool enable);

/** 播放单音：freq_hz 频率，ms 时长，vol 音量 0.0~1.0（阻塞） */
esp_err_t bsp_audio_play_tone(int freq_hz, int ms, float vol);

/** 开机提示音（两声短音） */
void bsp_audio_play_boot_sound(void);

/**
 * 读取麦克风采样（16bit 单声道 PCM，阻塞）。
 * 返回实际读到的采样数，负值表示出错。
 */
int bsp_audio_read_mic(int16_t *buf, size_t max_samples);

#ifdef __cplusplus
}
#endif
