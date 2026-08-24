/**
 * 模拟麦克风驱动 (ESP-IDF 版本)
 *
 * 当前为模拟音频源模式，不占用真实 I2S 麦克风，
 * 生成合成类鼾声音频数据供 MFCC / TFLM 推理使用。
 *
 * 优化：使用相位累加器替代 per-sample sinf(大数)，避免 __rem_pio2 死循环。
 */

#include "microphone_driver.h"
#include "app_config.h"
#include <esp_log.h>
#include <math.h>

static const char* TAG = "MIC";

// 音频参数
static const float SIM_FREQ_HZ     = 150.0f;   // 模拟鼾声基频
static const float BURST_PERIOD_S  = 3.0f;     // burst 周期
static const float BURST_DURATION_S= 0.8f;     // burst 持续时间
static const float SIM_AMPLITUDE   = 8000.0f;  // 幅度

// 相位累加器状态
static float s_carrier_phase = 0.0f;   // 载波相位 (0 ~ 2π)
static float s_envelope_phase = 0.0f;  // 包络相位 (0 ~ π)
static float s_burst_timer = 0.0f;     // burst 周期计时器 (0 ~ BURST_PERIOD_S)

bool microphone_init(void) {
    ESP_LOGI(TAG, "SIMULATE mode: fake microphone init (no I2S hardware used)");
    s_carrier_phase  = 0.0f;
    s_envelope_phase = 0.0f;
    s_burst_timer    = 0.0f;
    return true;
}

esp_err_t microphone_read(int16_t* buffer, size_t bytes_to_read, size_t* bytes_read) {
    size_t sample_count = bytes_to_read / sizeof(int16_t);

    // 每采样点相位增量
    const float carrier_increment = 2.0f * M_PI * SIM_FREQ_HZ / AUDIO_SAMPLE_RATE;
    const float envelope_increment = M_PI / (BURST_DURATION_S * AUDIO_SAMPLE_RATE);
    const float timer_increment = 1.0f / AUDIO_SAMPLE_RATE;

    for (size_t i = 0; i < sample_count; i++) {
        // 1. 更新 burst 计时器
        s_burst_timer += timer_increment;
        if (s_burst_timer >= BURST_PERIOD_S) {
            s_burst_timer -= BURST_PERIOD_S;
            s_envelope_phase = 0.0f;
        }

        // 2. 计算包络 (只在 burst 期间有声音)
        float envelope = 0.0f;
        if (s_burst_timer < BURST_DURATION_S) {
            envelope = sinf(s_envelope_phase);
            s_envelope_phase += envelope_increment;
            if (s_envelope_phase >= M_PI) {
                s_envelope_phase = M_PI;  // 保持为 0，直到下一个 burst
            }
        }

        // 3. 更新载波相位并限制在 0~2π
        s_carrier_phase += carrier_increment;
        if (s_carrier_phase >= 2.0f * M_PI) {
            s_carrier_phase -= 2.0f * M_PI;
        }

        // 4. 生成样本
        float sample = sinf(s_carrier_phase) * envelope * SIM_AMPLITUDE;
        buffer[i] = (int16_t)sample;
    }

    *bytes_read = bytes_to_read;
    return ESP_OK;
}
