/**
 * @file audio_processor.h
 * @brief Audio preprocessing and feature extraction for snore detection.
 *
 * Replicates the pipeline from the original Dart implementation:
 *   raw PCM → normalize → [Hamming window → FFT → power spectrum → dB] → features
 *
 * Input:   16000 samples (int16, 1 second @ 16 kHz)
 * Output:  4160 float features ready for TFLite inference
 *
 * ALL large buffers must be caller-allocated on PSRAM.
 * No stack arrays > 1KB inside any function.
 */

#ifndef AUDIO_PROCESSOR_H
#define AUDIO_PROCESSOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "model_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Safe PCM-to-float conversion with bounds checking.
 *
 * @param pcm         Input int16 PCM samples.
 * @param pcm_count   Number of input samples.
 * @param out         Output float buffer (caller-allocated).
 * @param out_capacity Max capacity of out buffer.
 * @return Number of samples converted, or 0 on error.
 *         If pcm_count > out_capacity, truncates and logs warning.
 */
size_t audio_processor_pcm_to_float(const int16_t *pcm, size_t pcm_count,
                                    float *out, size_t out_capacity);

/**
 * @brief Compute spectrogram features from a 1-second audio window.
 *
 * Pipeline per frame:
 *   1. Extract 320-sample frame with Hamming window
 *   2. Zero-pad to 128 samples, compute real FFT
 *   3. Power spectrum: mag^2
 *   4. dB scale: 10*log10(max(power, noise_floor_linear))
 *
 * @param pcm_samples   Array of SNORE_SAMPLES_PER_WINDOW int16 samples.
 * @param pcm_count     Number of samples (must be >= SNORE_SAMPLES_PER_WINDOW).
 * @param features_out  Output buffer of size SNORE_INPUT_SIZE (4160 floats).
 *                      Caller must pre-allocate on PSRAM.
 * @param work_buf      Temp buffer of SNORE_SAMPLES_PER_WINDOW floats (PSRAM).
 * @param fft_buf       FFT working buffer of 2*SNORE_FFT_SIZE floats (PSRAM or static).
 * @return 0 on success, -1 on error.
 */
int audio_processor_extract_features_safe(const int16_t *pcm_samples,
                                          size_t pcm_count,
                                          float *features_out,
                                          float *work_buf,
                                          float *fft_buf);

/**
 * @brief Resample audio from an arbitrary rate to 16 kHz via linear interpolation.
 *
 * @param src          Source samples.
 * @param src_len      Number of source samples.
 * @param src_rate     Original sample rate in Hz.
 * @param dst          Destination buffer (caller must allocate).
 * @param dst_max_len  Max capacity of dst buffer.
 * @return Number of samples written to dst, or -1 on overflow.
 */
int audio_processor_resample(const float *src, int src_len, int src_rate,
                             float *dst, int dst_max_len);

/**
 * @brief Legacy wrapper — use audio_processor_extract_features_safe instead.
 *        Kept for backward compatibility with snore_detector.cc.
 *        NOTE: this uses malloc internally, prefer the _safe variant.
 */
int audio_processor_extract_features(const int16_t *pcm_samples,
                                     float *features_out);

/**
 * @brief Compute lightweight time-domain features from PCM without FFT.
 *        Fast path for when TFLite model is OFF.
 *
 * @param pcm       Input PCM samples.
 * @param pcm_count Number of samples.
 * @param rms_out   Output RMS energy.
 * @param peak_out  Output peak value.
 * @param zcr_out   Output zero-crossing rate (0~1).
 */
void audio_processor_time_features(const int16_t *pcm, size_t pcm_count,
                                   float *rms_out, int16_t *peak_out, float *zcr_out);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_PROCESSOR_H */
