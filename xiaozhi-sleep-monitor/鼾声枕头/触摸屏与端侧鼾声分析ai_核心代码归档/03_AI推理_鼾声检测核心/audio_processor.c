/**
 * @file audio_processor.c
 * @brief Audio preprocessing — safe version with PSRAM buffers, no stack overflow.
 *
 * Pipeline (per 1-second window):
 *   int16 PCM → normalize → [Hamming window → 128-pt FFT → power → dB] * 64 → 4160 features
 *
 * The FFT implementation is a standalone radix-2 DIT for portability.
 */

#include "audio_processor.h"
#include "model_config.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "AUDIO_PROC";

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#ifndef M_LN10
#define M_LN10 2.302585092994046f
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 * Small radix-2 real-valued FFT (in-place, complex interleaved)
 * ═══════════════════════════════════════════════════════════════════════════ */

/** Bit-reverse reorder for length N (must be power of 2). */
static void fft_bit_reverse(float *data, int n) {
    int j = 0;
    for (int i = 0; i < n; i += 2) {
        if (j > i) {
            float tr = data[i], ti = data[i+1];
            data[i]   = data[j];
            data[i+1] = data[j+1];
            data[j]   = tr;
            data[j+1] = ti;
        }
        int m = n >> 1;
        while (m >= 2 && j >= m) {
            j -= m;
            m >>= 1;
        }
        j += m;
    }
}

/**
 * @brief In-place radix-2 DIT complex FFT.
 */
static void fft_radix2(float *data, int n, int inv) {
    fft_bit_reverse(data, n);

    for (int step = 2; step <= n; step <<= 1) {
        float angle = 2.0f * (float)M_PI / (float)step;
        if (inv) angle = -angle;
        float w_real = cosf(angle);
        float w_imag = sinf(angle);

        for (int i = 0; i < n; i += step * 2) {
            float cur_real = 1.0f;
            float cur_imag = 0.0f;

            for (int j = 0; j < step; j += 2) {
                int   even_idx = i + j;
                int   odd_idx  = even_idx + step;
                float even_real = data[even_idx];
                float even_imag = data[even_idx + 1];
                float odd_real  = data[odd_idx]  * cur_real - data[odd_idx+1] * cur_imag;
                float odd_imag  = data[odd_idx]  * cur_imag + data[odd_idx+1] * cur_real;

                data[even_idx]     = even_real + odd_real;
                data[even_idx + 1] = even_imag + odd_imag;
                data[odd_idx]      = even_real - odd_real;
                data[odd_idx + 1]  = even_imag - odd_imag;

                float tmp = cur_real * w_real - cur_imag * w_imag;
                cur_imag  = cur_real * w_imag + cur_imag * w_real;
                cur_real  = tmp;
            }
        }
    }

    if (inv) {
        for (int i = 0; i < 2 * n; i++) {
            data[i] /= (float)n;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Hamming window table — computed once
 * ═══════════════════════════════════════════════════════════════════════════ */

static float s_hamming[SNORE_FRAME_LENGTH_SAMPLES];
static bool  s_hamming_ready = false;

static void init_hamming(void) {
    if (s_hamming_ready) return;
    int n = SNORE_FRAME_LENGTH_SAMPLES;
    for (int i = 0; i < n; i++) {
        s_hamming[i] = 0.54f - 0.46f * cosf(2.0f * (float)M_PI * (float)i / (float)(n - 1));
    }
    s_hamming_ready = true;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API — safe versions
 * ═══════════════════════════════════════════════════════════════════════════ */

size_t audio_processor_pcm_to_float(const int16_t *pcm, size_t pcm_count,
                                     float *out, size_t out_capacity)
{
    if (!pcm || !out || out_capacity == 0) return 0;

    size_t n = pcm_count;
    if (n > out_capacity) {
        ESP_LOGW(TAG, "pcm_to_float: truncating %u → %u samples", (unsigned)pcm_count, (unsigned)out_capacity);
        n = out_capacity;
    }

    for (size_t i = 0; i < n; i++) {
        out[i] = (float)pcm[i] / 32768.0f;
    }
    return n;
}

int audio_processor_extract_features_safe(const int16_t *pcm_samples,
                                           size_t pcm_count,
                                           float *features_out,
                                           float *work_buf,
                                           float *fft_buf)
{
    if (!pcm_samples || !features_out || !work_buf || !fft_buf) return -1;
    if (pcm_count < 4000) return -1;  /* 至少 250ms 音频 */

    init_hamming();

    /* Step 1: int16 → float — 零初始化 work_buf，只转换实际有的样本 */
    memset(work_buf, 0, SNORE_SAMPLES_PER_WINDOW * sizeof(float));
    size_t to_convert = (pcm_count < (size_t)SNORE_SAMPLES_PER_WINDOW)
                        ? pcm_count : (size_t)SNORE_SAMPLES_PER_WINDOW;
    size_t converted = audio_processor_pcm_to_float(pcm_samples, to_convert,
                                                      work_buf, SNORE_SAMPLES_PER_WINDOW);
    if (converted == 0) return -1;

    /* Step 2: normalize to [-1, 1] */
    float max_abs = 0.0f;
    for (int i = 0; i < SNORE_SAMPLES_PER_WINDOW; i++) {
        float a = fabsf(work_buf[i]);
        if (a > max_abs) max_abs = a;
    }
    if (max_abs > 0.0f) {
        for (int i = 0; i < SNORE_SAMPLES_PER_WINDOW; i++) {
            work_buf[i] /= max_abs;
        }
    }

    /* Step 3: frame extraction & spectrogram */
    const int frame_len    = SNORE_FRAME_LENGTH_SAMPLES;   /* 320 */
    const int frame_stride = SNORE_FRAME_STRIDE_SAMPLES;   /* 246 */
    const int num_bins     = SNORE_FFT_BINS;               /* 65  */
    int       feat_idx     = 0;

    for (int f = 0; f < SNORE_FRAMES_PER_WINDOW; f++) {
        int start = f * frame_stride;
        if (start + frame_len > SNORE_SAMPLES_PER_WINDOW) break;

        /* 3a. Apply Hamming window & copy to FFT buffer (zero-padded to 128) */
        memset(fft_buf, 0, 2 * SNORE_FFT_SIZE * sizeof(float));

        for (int i = 0; i < frame_len; i++) {
            fft_buf[2 * i] = work_buf[start + i] * s_hamming[i];
        }

        /* 3b. FFT */
        fft_radix2(fft_buf, SNORE_FFT_SIZE, 0);

        /* 3c–3e. Magnitude → Power → dB */
        for (int k = 0; k < num_bins; k++) {
            float real = fft_buf[2 * k];
            float imag = fft_buf[2 * k + 1];
            float mag  = sqrtf(real * real + imag * imag);
            float power = mag * mag;

            if (power < SNORE_NOISE_FLOOR_LINEAR) {
                power = SNORE_NOISE_FLOOR_LINEAR;
            }

            float db = 10.0f * logf(power) / M_LN10;
            features_out[feat_idx++] = db;
        }
    }

    /* Step 4: pad to exact 4160 features */
    while (feat_idx < SNORE_INPUT_SIZE) {
        features_out[feat_idx++] = (float)SNORE_NOISE_FLOOR_DB;
    }

    return 0;
}

int audio_processor_extract_features(const int16_t *pcm_samples,
                                     float *features_out)
{
    if (!pcm_samples || !features_out) return -1;

    init_hamming();

    /* Legacy malloc-based — prefer _safe version to avoid fragmentation */
    float *audio_float = (float *)malloc(SNORE_SAMPLES_PER_WINDOW * sizeof(float));
    if (!audio_float) return -1;

    audio_processor_pcm_to_float(pcm_samples, SNORE_SAMPLES_PER_WINDOW,
                                  audio_float, SNORE_SAMPLES_PER_WINDOW);

    /* Normalize */
    float max_abs = 0.0f;
    for (int i = 0; i < SNORE_SAMPLES_PER_WINDOW; i++) {
        float a = fabsf(audio_float[i]);
        if (a > max_abs) max_abs = a;
    }
    if (max_abs > 0.0f) {
        for (int i = 0; i < SNORE_SAMPLES_PER_WINDOW; i++) {
            audio_float[i] /= max_abs;
        }
    }

    const int frame_len    = SNORE_FRAME_LENGTH_SAMPLES;
    const int frame_stride = SNORE_FRAME_STRIDE_SAMPLES;
    const int num_bins     = SNORE_FFT_BINS;
    int       feat_idx     = 0;

    /* FFT buffer on heap to avoid stack overflow */
    float *fft_data = (float *)calloc(2 * SNORE_FFT_SIZE, sizeof(float));
    if (!fft_data) { free(audio_float); return -1; }

    for (int f = 0; f < SNORE_FRAMES_PER_WINDOW; f++) {
        int start = f * frame_stride;
        if (start + frame_len > SNORE_SAMPLES_PER_WINDOW) break;

        memset(fft_data, 0, 2 * SNORE_FFT_SIZE * sizeof(float));

        for (int i = 0; i < frame_len; i++) {
            fft_data[2 * i] = audio_float[start + i] * s_hamming[i];
        }

        fft_radix2(fft_data, SNORE_FFT_SIZE, 0);

        for (int k = 0; k < num_bins; k++) {
            float real = fft_data[2 * k];
            float imag = fft_data[2 * k + 1];
            float mag  = sqrtf(real * real + imag * imag);
            float power = mag * mag;

            if (power < SNORE_NOISE_FLOOR_LINEAR) {
                power = SNORE_NOISE_FLOOR_LINEAR;
            }

            float db = 10.0f * logf(power) / M_LN10;
            features_out[feat_idx++] = db;
        }
    }

    while (feat_idx < SNORE_INPUT_SIZE) {
        features_out[feat_idx++] = (float)SNORE_NOISE_FLOOR_DB;
    }

    free(fft_data);
    free(audio_float);
    return 0;
}

int audio_processor_resample(const float *src, int src_len, int src_rate,
                             float *dst, int dst_max_len) {
    if (src_rate == SNORE_TARGET_SAMPLE_RATE) {
        if (src_len > dst_max_len) return -1;
        memcpy(dst, src, src_len * sizeof(float));
        return src_len;
    }

    float ratio = (float)SNORE_TARGET_SAMPLE_RATE / (float)src_rate;
    int new_len = (int)((float)src_len * ratio);
    if (new_len > dst_max_len) return -1;

    for (int i = 0; i < new_len; i++) {
        float src_idx = (float)i / ratio;
        int   idx1    = (int)src_idx;
        int   idx2    = (idx1 + 1 < src_len) ? idx1 + 1 : src_len - 1;
        float frac    = src_idx - (float)idx1;
        dst[i] = src[idx1] * (1.0f - frac) + src[idx2] * frac;
    }
    return new_len;
}

void audio_processor_time_features(const int16_t *pcm, size_t pcm_count,
                                    float *rms_out, int16_t *peak_out, float *zcr_out)
{
    if (!pcm || pcm_count == 0) {
        if (rms_out)  *rms_out  = 0;
        if (peak_out) *peak_out = 0;
        if (zcr_out)  *zcr_out  = 0;
        return;
    }

    float sum_sq = 0;
    int16_t peak = 0;
    int zc = 0;

    for (size_t i = 0; i < pcm_count; i++) {
        int16_t v = pcm[i];
        sum_sq += (float)v * v;
        if (v > peak) peak = v;
        else if (-v > peak) peak = -v;
        if (i > 0 && ((pcm[i-1] ^ v) < 0)) zc++;
    }

    if (rms_out)  *rms_out  = sqrtf(sum_sq / (float)pcm_count);
    if (peak_out) *peak_out = peak;
    if (zcr_out)  *zcr_out  = (float)zc / (float)pcm_count;
}
