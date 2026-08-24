/**
 * snore_micro_feature_adapter.cc v2
 *
 * 将 16000 点 int16 PCM → microfrontend → 1830 uint8 特征 (61×30)。
 * 每次推理前 reset 前端状态，确保特征只来自当前 1 秒音频。
 *
 * 时间: 32ms window, 16ms stride → 61 frames from 16000 samples @ 16kHz
 */

#include "snore_micro_feature_adapter.h"
#include "microfrontend/frontend.h"
#include "microfrontend/frontend_util.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <cstring>

static const char *TAG = "MICRO_FEAT";

#define MICRO_REQUIRED_SAMPLES  16000
#define MICRO_FRONTEND_RESET_EACH_INFER 1

static bool          s_initialised = false;
static FrontendState s_frontend_state;

extern "C" {

bool snore_micro_feature_init(void)
{
    if (s_initialised) return true;

    FrontendConfig config;
    config.window.size_ms = 32;
    config.window.step_size_ms = 16;
    config.noise_reduction.smoothing_bits = 10;
    config.filterbank.num_channels = MICRO_FEATURE_FREQ_SLICES;  /* 30 */
    config.filterbank.lower_band_limit = 40.0;
    config.filterbank.upper_band_limit = 6000.0;
    config.noise_reduction.even_smoothing = 0.025;
    config.noise_reduction.odd_smoothing = 0.06;
    config.noise_reduction.min_signal_remaining = 0.05;
    config.pcan_gain_control.enable_pcan = 1;
    config.pcan_gain_control.strength = 0.95;
    config.pcan_gain_control.offset = 80.0;
    config.pcan_gain_control.gain_bits = 21;
    config.log_scale.enable_log = 1;
    config.log_scale.scale_shift = 6;

    if (!FrontendPopulateState(&config, &s_frontend_state, MICRO_AUDIO_SAMPLE_RATE)) {
        ESP_LOGE(TAG, "FrontendPopulateState failed");
        return false;
    }

    s_initialised = true;
    ESP_LOGI(TAG, "Microfrontend init OK (30×61, 16kHz, 40-6000Hz, full 1s window)");
    return true;
}

bool snore_micro_generate_features(const int16_t *pcm_samples, size_t pcm_count,
                                    uint8_t *features_out)
{
    if (!s_initialised || !pcm_samples || !features_out) return false;
    if (pcm_count < MICRO_REQUIRED_SAMPLES) {
        ESP_LOGW(TAG, "PCM too short: %u < %u", (unsigned)pcm_count, (unsigned)MICRO_REQUIRED_SAMPLES);
        return false;
    }

    /* ── 每次推理前清零输出 + reset 前端状态 ── */
    memset(features_out, 0, MICRO_FEATURE_ELEMENT_COUNT);
#if MICRO_FRONTEND_RESET_EACH_INFER
    FrontendReset(&s_frontend_state);
#endif

    /* ── 用完整 16000 点生成 61 个 time slice ── */
    size_t samples_consumed_total = 0;
    int slices_generated = 0;

    for (int i = 0; i < MICRO_FEATURE_TIME_SLICES; i++) {
        uint8_t *slice_out = features_out + (i * MICRO_FEATURE_FREQ_SLICES);

        size_t num_read = 0;
        FrontendOutput out = FrontendProcessSamples(
            &s_frontend_state,
            pcm_samples + (int)samples_consumed_total,
            (int)(MICRO_REQUIRED_SAMPLES - samples_consumed_total),
            &num_read);

        samples_consumed_total += num_read;

        if (out.size > 0 && out.values) {
            slices_generated++;
            for (size_t j = 0; j < out.size && j < MICRO_FEATURE_FREQ_SLICES; j++) {
                int32_t value = ((int32_t)out.values[j] * 128) / 335;
                if (value < 0)   value = 0;
                if (value > 255) value = 255;
                slice_out[j] = (uint8_t)value;
            }
        } else {
            /* 这一帧没产出，微前端可能还在积攒样本，继续 */
        }
    }

    /* ── 验证：必须生成完整 61 帧 ── */
    if (slices_generated < MICRO_FEATURE_TIME_SLICES) {
        ESP_LOGW(TAG, "Incomplete features: %d/%d slices, consumed %u samples",
                 slices_generated, MICRO_FEATURE_TIME_SLICES,
                 (unsigned)samples_consumed_total);
        return false;
    }

    return true;
}

} /* extern "C" */
