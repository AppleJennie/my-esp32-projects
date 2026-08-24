/**
 * 特征提取器
 * 
 * 音频: 简化版 MFCC (13维 x 49帧)
 * 雷达: 时域/频域特征 (64维)
 * 生理: 统计特征 (32维)
 * 
 * 由于ESP32资源有限，使用轻量级算法
 */

#include "feature_extractor.h"
#include "app_config.h"
#include "dsp_utils.h"
#include <math.h>
#include <string.h>
#include <esp_dsp.h>
#include <dsps_fft2r.h>

#ifndef CONFIG_DSP_MAX_FFT_SIZE
#define CONFIG_DSP_MAX_FFT_SIZE 4096
#endif

static const char* TAG = "FEATURE";

// FFT 缓冲区 (使用 ESP-DSP)
static float* s_fft_buffer = NULL;
static float* s_window = NULL;
static int s_fft_size = 512;

// Mel 滤波器组 (简化: 26个三角滤波器)
#define MEL_FILTERS     26
#define MEL_FREQ_LOW    80.0f
#define MEL_FREQ_HIGH   4000.0f
static float* s_mel_filterbank = nullptr;  // PSRAM 动态分配: MEL_FILTERS * 256

// Hamming 窗
static void init_hamming_window(float* window, int size) {
    for (int i = 0; i < size; i++) {
        window[i] = 0.54f - 0.46f * cosf(2.0f * M_PI * i / (size - 1));
    }
}

// 频率 -> Mel
static inline float freq_to_mel(float freq) {
    return 2595.0f * log10f(1.0f + freq / 700.0f);
}

// Mel -> 频率
static inline float mel_to_freq(float mel) {
    return 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f);
}

// 初始化 Mel 滤波器组
static void init_mel_filterbank(void) {
    float mel_low = freq_to_mel(MEL_FREQ_LOW);
    float mel_high = freq_to_mel(MEL_FREQ_HIGH);
    float mel_step = (mel_high - mel_low) / (MEL_FILTERS + 1);
    
    float mel_centers[MEL_FILTERS + 2];
    float freq_centers[MEL_FILTERS + 2];
    int fft_bins[MEL_FILTERS + 2];
    
    for (int i = 0; i < MEL_FILTERS + 2; i++) {
        mel_centers[i] = mel_low + i * mel_step;
        freq_centers[i] = mel_to_freq(mel_centers[i]);
        fft_bins[i] = (int)(freq_centers[i] * s_fft_size / AUDIO_SAMPLE_RATE);
        if (fft_bins[i] >= s_fft_size / 2) fft_bins[i] = s_fft_size / 2 - 1;
    }
    
    // 计算三角滤波器
    for (int m = 1; m <= MEL_FILTERS; m++) {
        for (int k = fft_bins[m - 1]; k <= fft_bins[m + 1] && k < s_fft_size / 2; k++) {
            if (k <= fft_bins[m]) {
                s_mel_filterbank[(m - 1) * 256 + k] = (float)(k - fft_bins[m - 1]) / (fft_bins[m] - fft_bins[m - 1]);
            } else {
                s_mel_filterbank[(m - 1) * 256 + k] = (float)(fft_bins[m + 1] - k) / (fft_bins[m + 1] - fft_bins[m]);
            }
        }
    }
}

bool feature_extractor_init(void) {
    // 初始化 ESP-DSP FFT
    esp_err_t err = dsps_fft2r_init_fc32(NULL, s_fft_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP-DSP FFT init failed: %s", esp_err_to_name(err));
        return false;
    }

    // 分配 PSRAM 中的缓冲区
    // 注意: dsps_fft2r_fc32 需要交错复数格式，大小为 2 * N
    s_fft_buffer = (float*)heap_caps_calloc(s_fft_size * 2, sizeof(float), MALLOC_CAP_SPIRAM);
    s_window = (float*)heap_caps_malloc(s_fft_size * sizeof(float), MALLOC_CAP_SPIRAM);
    s_mel_filterbank = (float*)heap_caps_calloc(MEL_FILTERS * 256, sizeof(float), MALLOC_CAP_SPIRAM);
    
    if (!s_fft_buffer || !s_window || !s_mel_filterbank) {
        ESP_LOGE(TAG, "Failed to allocate feature buffers in PSRAM");
        return false;
    }
    
    init_hamming_window(s_window, s_fft_size);
    init_mel_filterbank();
    
    ESP_LOGI(TAG, "Feature extractor initialized, FFT size=%d", s_fft_size);
    return true;
}

// 计算一帧 MFCC (13维)
static void compute_mfcc_frame(const int16_t* samples, float* mfcc_out) {
    // 1. 预加重
    float preemph[s_fft_size];
    preemph[0] = samples[0] / 32768.0f;
    for (int i = 1; i < s_fft_size; i++) {
        preemph[i] = samples[i] / 32768.0f - 0.97f * (samples[i - 1] / 32768.0f);
    }
    
    // 2. 加窗 (填入交错复数格式: [re0, im0, re1, im1, ...])
    for (int i = 0; i < s_fft_size; i++) {
        s_fft_buffer[i * 2] = preemph[i] * s_window[i];
        s_fft_buffer[i * 2 + 1] = 0.0f;
    }
    
    // 3. FFT (使用 ESP-DSP)
    // 注意: 这里使用简化的功率谱计算
    dsps_fft2r_fc32(s_fft_buffer, s_fft_size);
    dsps_bit_rev_fc32(s_fft_buffer, s_fft_size);
    
    float power_spectrum[s_fft_size / 2];
    for (int i = 0; i < s_fft_size / 2; i++) {
        float real = s_fft_buffer[i * 2];
        float imag = s_fft_buffer[i * 2 + 1];
        power_spectrum[i] = real * real + imag * imag;
    }
    
    // 4. Mel 滤波
    float mel_energies[MEL_FILTERS];
    for (int m = 0; m < MEL_FILTERS; m++) {
        mel_energies[m] = 0.0f;
        for (int k = 0; k < s_fft_size / 2; k++) {
            mel_energies[m] += power_spectrum[k] * s_mel_filterbank[m * 256 + k];
        }
        mel_energies[m] = logf(mel_energies[m] + 1e-10f);
    }
    
    // 5. DCT -> 13 维 MFCC (简化版，只取前13个)
    for (int n = 0; n < AUDIO_MFCC_NUM; n++) {
        mfcc_out[n] = 0.0f;
        for (int m = 0; m < MEL_FILTERS; m++) {
            mfcc_out[n] += mel_energies[m] * cosf(M_PI * n * (m + 0.5f) / MEL_FILTERS);
        }
    }
}

void extract_audio_features(const int16_t* audio_samples, int sample_count, float* features) {
    // 分帧: 每帧 512 样本 (32ms),  hop 320 (20ms) -> 49帧/3秒
    int frame_size = s_fft_size;        // 512
    int hop_size = 320;                 // 20ms @ 16kHz
    int num_frames = sample_count / hop_size;
    if (num_frames > AUDIO_MFCCFrames_PER_INF) num_frames = AUDIO_MFCCFrames_PER_INF;
    
    for (int f = 0; f < num_frames; f++) {
        int start = f * hop_size;
        int16_t frame[512];
        
        // 复制一帧 (带零填充)
        for (int i = 0; i < frame_size; i++) {
            if (start + i < sample_count) {
                frame[i] = audio_samples[start + i];
            } else {
                frame[i] = 0;
            }
        }
        
        compute_mfcc_frame(frame, &features[f * AUDIO_MFCC_NUM]);
    }
    
    // 零填充剩余帧
    for (int f = num_frames; f < AUDIO_MFCCFrames_PER_INF; f++) {
        memset(&features[f * AUDIO_MFCC_NUM], 0, AUDIO_MFCC_NUM * sizeof(float));
    }
}

void extract_radar_features(const float* radar_samples, int sample_count, float* features) {
    if (sample_count < 20) {
        memset(features, 0, RADAR_FEATURE_SIZE * sizeof(float));
        return;
    }
    
    // 时域特征
    float mean = 0.0f, std = 0.0f, max_val = -1e10f, min_val = 1e10f;
    float zero_cross = 0.0f;
    
    for (int i = 0; i < sample_count; i++) {
        mean += radar_samples[i];
        if (radar_samples[i] > max_val) max_val = radar_samples[i];
        if (radar_samples[i] < min_val) min_val = radar_samples[i];
        if (i > 0 && radar_samples[i] * radar_samples[i - 1] < 0) {
            zero_cross += 1.0f;
        }
    }
    mean /= sample_count;
    
    for (int i = 0; i < sample_count; i++) {
        float diff = radar_samples[i] - mean;
        std += diff * diff;
    }
    std = sqrtf(std / sample_count);
    
    // 呼吸率估计 (过零率 / 2 * 采样率)
    float est_resp_rate = (zero_cross / 2.0f) * (RADAR_SAMPLE_RATE / sample_count) * 60.0f;
    
    // 频域特征 (简化 FFT)
    // 这里填充特征向量
    features[0] = mean;
    features[1] = std;
    features[2] = max_val - min_val;  // 峰峰值
    features[3] = max_val;
    features[4] = min_val;
    features[5] = zero_cross / sample_count;
    features[6] = est_resp_rate;
    features[7] = std / (fabsf(mean) + 1e-6f);  // 变异系数
    
    // 统计矩
    float skew = 0.0f, kurt = 0.0f;
    for (int i = 0; i < sample_count; i++) {
        float z = (radar_samples[i] - mean) / (std + 1e-6f);
        skew += z * z * z;
        kurt += z * z * z * z;
    }
    features[8] = skew / sample_count;
    features[9] = kurt / sample_count - 3.0f;
    
    // 自相关特征 (滞后1-5)
    for (int lag = 1; lag <= 5 && lag < sample_count; lag++) {
        float ac = 0.0f;
        for (int i = 0; i < sample_count - lag; i++) {
            ac += (radar_samples[i] - mean) * (radar_samples[i + lag] - mean);
        }
        features[10 + lag - 1] = ac / (sample_count - lag) / (std * std + 1e-6f);
    }
    
    // 填充剩余特征
    for (int i = 15; i < RADAR_FEATURE_SIZE; i++) {
        features[i] = 0.0f;
    }
}

void extract_physio_features(const float* physio_samples, int sample_count, float* features) {
    // physio_samples 是 [N][4] 数组: spo2, hr, ppg, temp
    if (sample_count < 10) {
        memset(features, 0, PHYSIO_FEATURE_SIZE * sizeof(float));
        return;
    }
    
    // 分别提取每个通道的统计特征
    int feat_per_channel = PHYSIO_FEATURE_SIZE / 4;  // 8 features per channel
    
    for (int c = 0; c < 4; c++) {
        float data[PHYSIO_HISTORY_SIZE];
        int valid_count = 0;
        
        for (int i = 0; i < sample_count; i++) {
            float val = physio_samples[i * 4 + c];
            if (val > 0) {  // 有效数据
                data[valid_count++] = val;
            }
        }
        
        if (valid_count == 0) {
            memset(&features[c * feat_per_channel], 0, feat_per_channel * sizeof(float));
            continue;
        }
        
        // 均值
        float mean = 0.0f;
        for (int i = 0; i < valid_count; i++) mean += data[i];
        mean /= valid_count;
        
        // 标准差
        float std = 0.0f;
        for (int i = 0; i < valid_count; i++) {
            float d = data[i] - mean;
            std += d * d;
        }
        std = sqrtf(std / valid_count);
        
        // 最值
        float min_v = data[0], max_v = data[0];
        for (int i = 1; i < valid_count; i++) {
            if (data[i] < min_v) min_v = data[i];
            if (data[i] > max_v) max_v = data[i];
        }
        
        // 趋势 (线性回归斜率)
        float slope = 0.0f;
        if (valid_count > 1) {
            float sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;
            for (int i = 0; i < valid_count; i++) {
                sum_x += i;
                sum_y += data[i];
                sum_xy += i * data[i];
                sum_x2 += i * i;
            }
            slope = (valid_count * sum_xy - sum_x * sum_y) / 
                    (valid_count * sum_x2 - sum_x * sum_x + 1e-6f);
        }
        
        features[c * feat_per_channel + 0] = mean;
        features[c * feat_per_channel + 1] = std;
        features[c * feat_per_channel + 2] = min_v;
        features[c * feat_per_channel + 3] = max_v;
        features[c * feat_per_channel + 4] = max_v - min_v;
        features[c * feat_per_channel + 5] = slope;
        features[c * feat_per_channel + 6] = std / (fabsf(mean) + 1e-6f);
        features[c * feat_per_channel + 7] = (data[valid_count - 1] - data[0]) / (valid_count + 1e-6f);
    }
}
