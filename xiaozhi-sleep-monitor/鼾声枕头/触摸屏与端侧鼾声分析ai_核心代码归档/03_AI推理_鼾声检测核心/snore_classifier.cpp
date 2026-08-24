/**
 * @file snore_classifier.cc
 * @brief Rule-based snore type classification — simplified spectral approach.
 *
 * Uses 3 acoustic features:
 *   - spectral_centroid (Hz)
 *   - low_freq_ratio (0~1)
 *   - harmonic_ratio (0~1)
 *
 * Reference:
 *   - Jane et al., "Snoring analysis..." (IEEE EMBS, 2000)
 *   - Duckitt et al., "Automatic detection and classification of snoring..."
 */

#include "snore_classifier.h"
#include "model_config.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* ── Suggest strings ───────────────────────────────────────────────────── */
static const char *suggest_nasal  = "疑似鼻鼾，可能与鼻腔通气不畅有关，建议关注鼻塞、空气湿度和睡姿。";
static const char *suggest_throat = "疑似喉鼾，可能存在咽部软组织振动，建议结合雷达呼吸波形观察阻塞风险。";
static const char *suggest_mouth  = "疑似口呼吸，建议关注张口睡眠、口干和鼻腔通气情况。";
static const char *suggest_mixed  = "混合型鼾声，建议结合呼吸率、体动和夜间事件综合判断。";
static const char *suggest_none   = "未检测到明显鼾声。";

/* ═════════════════════════════════════════════════════════════════════════
 * Main classification — uses 3 lightweight metrics
 * ═════════════════════════════════════════════════════════════════════════ */

void snore_classifier_classify(float spectral_centroid,
                               float low_freq_ratio,
                               float harmonic_ratio,
                               bool is_snoring,
                               snore_classification_t *result)
{
    if (!result) return;
    memset(result, 0, sizeof(*result));
    result->type    = SNORE_TYPE_NONE;
    result->label   = "none";
    result->suggest = suggest_none;

    if (!is_snoring) return;

    float score_nasal  = 0.0f;
    float score_throat = 0.0f;
    float score_mouth  = 0.0f;

    /* ── Nasal: centroid 600~1500Hz, harmonic > 0.35 ── */
    {
        float c_ok = 0.0f, h_ok = 0.0f;
        if      (spectral_centroid >= 600.0f && spectral_centroid <= 1500.0f) c_ok = 1.0f;
        else if (spectral_centroid >= 400.0f && spectral_centroid <  600.0f)  c_ok = 0.5f;
        else if (spectral_centroid > 1500.0f && spectral_centroid <= 2000.0f) c_ok = 0.5f;

        if      (harmonic_ratio >= 0.35f) h_ok = 1.0f;
        else if (harmonic_ratio >= 0.25f) h_ok = 0.6f;

        score_nasal = c_ok * 0.5f + h_ok * 0.5f;
    }

    /* ── Throat: centroid 150~600Hz, low_freq > 0.5 ── */
    {
        float c_ok = 0.0f, lf_ok = 0.0f;
        if      (spectral_centroid >= 150.0f && spectral_centroid <= 600.0f)  c_ok = 1.0f;
        else if (spectral_centroid >= 100.0f && spectral_centroid <  150.0f)  c_ok = 0.6f;
        else if (spectral_centroid >  600.0f && spectral_centroid <= 800.0f)  c_ok = 0.5f;

        if      (low_freq_ratio > 0.55f) lf_ok = 1.0f;
        else if (low_freq_ratio > 0.40f) lf_ok = 0.6f;

        score_throat = c_ok * 0.5f + lf_ok * 0.5f;
    }

    /* ── Mouth: centroid > 1500Hz, harmonic < 0.2 ── */
    {
        float c_ok = 0.0f, h_ok = 0.0f;
        if      (spectral_centroid > 1500.0f) c_ok = 1.0f;
        else if (spectral_centroid > 1000.0f) c_ok = 0.5f;

        if      (harmonic_ratio < 0.2f)  h_ok = 1.0f;
        else if (harmonic_ratio < 0.3f)  h_ok = 0.5f;

        score_mouth = c_ok * 0.5f + h_ok * 0.5f;
    }

    /* ── Decision ── */
    if (score_nasal < 0.3f && score_throat < 0.3f && score_mouth < 0.3f) {
        result->type       = SNORE_TYPE_MIXED;
        result->confidence = fmaxf(fmaxf(score_nasal, score_throat), score_mouth);
        result->label      = "mixed";
        result->suggest    = suggest_mixed;
        return;
    }

    if (score_nasal >= score_throat && score_nasal >= score_mouth) {
        result->type       = SNORE_TYPE_NASAL;
        result->confidence = score_nasal;
        result->label      = "nasal";
        result->suggest    = suggest_nasal;
    } else if (score_throat >= score_nasal && score_throat >= score_mouth) {
        result->type       = SNORE_TYPE_THROAT;
        result->confidence = score_throat;
        result->label      = "throat";
        result->suggest    = suggest_throat;
    } else {
        result->type       = SNORE_TYPE_MOUTH;
        result->confidence = score_mouth;
        result->label      = "mouth";
        result->suggest    = suggest_mouth;
    }
}

/* ── Legacy: full spectrogram classification ──────────────────────────── */

/* dB → linear power */
static inline float db_to_linear(float db) {
    if (db < -60.0f) return 0.0f;
    return powf(10.0f, db / 10.0f);
}

static void compute_averaged_spectrum(const float *features, float *avg_power) {
    memset(avg_power, 0, SNORE_FFT_BINS * sizeof(float));
    for (int frame = 0; frame < SNORE_FRAMES_PER_WINDOW; frame++) {
        const float *fp = features + frame * SNORE_FFT_BINS;
        for (int bin = 0; bin < SNORE_FFT_BINS; bin++) {
            avg_power[bin] += db_to_linear(fp[bin]);
        }
    }
    for (int bin = 0; bin < SNORE_FFT_BINS; bin++) {
        avg_power[bin] /= (float)SNORE_FRAMES_PER_WINDOW;
    }
}

void snore_classifier_predict(const float *features,
                              snore_classification_t *result)
{
    if (!result) return;
    memset(result, 0, sizeof(*result));

    if (!features) {
        result->type    = SNORE_TYPE_MIXED;
        result->label   = "mixed";
        result->suggest = suggest_mixed;
        return;
    }

    float avg_power[SNORE_FFT_BINS];
    compute_averaged_spectrum(features, avg_power);

    /* Simple spectral centroid and low-freq ratio from averaged spectrum */
    float total_power = 1e-6f, low_e = 0, weighted_f = 0;
    for (int bin = 0; bin < SNORE_FFT_BINS; bin++) {
        float p = avg_power[bin];
        float hz = (float)bin * 125.0f;
        total_power += p;
        weighted_f  += p * hz;
        if (hz < 500.0f) low_e += p;
    }
    float centroid = weighted_f / total_power;
    float lf_ratio = low_e / total_power;

    /* Approximate harmonic ratio */
    int peak_count = 0;
    for (int bin = 1; bin < SNORE_FFT_BINS - 1; bin++) {
        if (avg_power[bin] > avg_power[bin-1] &&
            avg_power[bin] > avg_power[bin+1] &&
            avg_power[bin] > 0.001f) peak_count++;
    }
    float harmonic = (peak_count > 3 && peak_count < 30) ? 0.35f + peak_count * 0.02f : 0.15f;
    if (harmonic > 0.8f) harmonic = 0.8f;

    snore_classifier_classify(centroid, lf_ratio, harmonic, true, result);
}

const char *snore_type_label(snore_type_t type) {
    switch (type) {
        case SNORE_TYPE_NASAL:  return "nasal";
        case SNORE_TYPE_THROAT: return "throat";
        case SNORE_TYPE_MOUTH:  return "mouth";
        case SNORE_TYPE_MIXED:  return "mixed";
        default:                return "none";
    }
}
