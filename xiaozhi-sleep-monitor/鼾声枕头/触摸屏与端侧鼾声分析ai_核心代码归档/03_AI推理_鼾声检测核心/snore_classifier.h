/**
 * @file snore_classifier.h
 * @brief Rule-based snore type classification using spectral features.
 *
 * Pipeline:
 *   audio → extract 4160 features → TFLite says "snoring?"
 *                                     ↓ yes
 *                                 snore_classifier_predict() → NASAL / THROAT / MOUTH / MIXED
 */

#ifndef SNORE_CLASSIFIER_H
#define SNORE_CLASSIFIER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Snore type enum ──────────────────────────────────────────────────── */
typedef enum {
    SNORE_TYPE_NONE    = 0,
    SNORE_TYPE_NASAL   = 1,
    SNORE_TYPE_THROAT  = 2,
    SNORE_TYPE_MOUTH   = 3,
    SNORE_TYPE_MIXED   = 4,
    SNORE_TYPE_UNKNOWN = 5
} snore_type_t;

/* ── Simplified result ────────────────────────────────────────────────── */
typedef struct {
    snore_type_t type;
    float        confidence;
    const char  *label;
    const char  *suggest;
} snore_classification_t;

/* ── API ──────────────────────────────────────────────────────────────── */

/**
 * @brief Classify snore type from simple spectral metrics.
 *
 * Designed to work WITHOUT the full 4160-feature spectrogram.
 * Uses spectral_centroid, low_freq_ratio, and harmonic_ratio only.
 *
 * @param spectral_centroid  Weighted mean frequency in Hz.
 * @param low_freq_ratio     Fraction of energy below 500 Hz.
 * @param harmonic_ratio     Fraction of energy in harmonic peaks.
 * @param is_snoring         Whether TFLite (or rules) flagged this as snoring.
 * @param result             Output struct.
 */
void snore_classifier_classify(float spectral_centroid,
                               float low_freq_ratio,
                               float harmonic_ratio,
                               bool is_snoring,
                               snore_classification_t *result);

/**
 * @brief Legacy wrapper — works with full 4160-feature spectrogram.
 *        Only call this when TFLite says is_snoring=true.
 */
void snore_classifier_predict(const float *spectrogram_features,
                              snore_classification_t *result);

const char *snore_type_label(snore_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* SNORE_CLASSIFIER_H */
