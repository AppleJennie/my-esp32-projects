/**
 * radar_breath_event.c — 雷达呼吸事件检测
 */
#include "radar_breath_event.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "esp_log.h"

__attribute__((unused)) static const char *TAG = "BREATH_EVT";

static breath_event_t s_event;

/* 历史缓冲 */
#define BUF_LEN 30  /* 30 秒 */
static float s_amp_hist[BUF_LEN];
static int   s_amp_idx = 0;
static int   s_amp_cnt = 0;
static float s_motion_hist[BUF_LEN];
static int   s_motion_idx = 0;
static int   s_motion_cnt = 0;
static float s_bpm_hist[BUF_LEN];
static int   s_bpm_idx = 0;
static int   s_bpm_cnt = 0;

/* 低幅度持续时间 */
static int s_low_amp_sec = 0;
static int s_pause_sec   = 0;
static bool s_low_or_pause_latched = false;

void radar_breath_event_init(void)
{
    memset(&s_event, 0, sizeof(s_event));
    memset(s_amp_hist, 0, sizeof(s_amp_hist));
    memset(s_motion_hist, 0, sizeof(s_motion_hist));
    memset(s_bpm_hist, 0, sizeof(s_bpm_hist));
    s_amp_idx = s_amp_cnt = 0;
    s_motion_idx = s_motion_cnt = 0;
    s_bpm_idx = s_bpm_cnt = 0;
    s_low_amp_sec = 0;
    s_pause_sec = 0;
    s_low_or_pause_latched = false;
    s_event.type = BREATH_EVT_UNKNOWN;
}

void radar_breath_event_feed(const radar_feature_t *rf, const baseline_t *bl, uint32_t now_ms)
{
    (void)now_ms;
    memset(&s_event, 0, sizeof(s_event));
    s_event.type = BREATH_EVT_UNKNOWN;

    if (!rf) { s_event.type = BREATH_EVT_DATA_INVALID; return; }

    /* ── 数据有效性 ── */
    if (!rf->radar_connected) {
        s_event.type = BREATH_EVT_DATA_INVALID;
        snprintf(s_event.reason, sizeof(s_event.reason), "radar disconnected");
        return;
    }
    if (!rf->presence_valid && !rf->presence_inferred) {
        s_event.type = BREATH_EVT_DATA_INVALID;
        snprintf(s_event.reason, sizeof(s_event.reason), "no presence");
        return;
    }
    if (rf->bed_valid && !rf->in_bed) {
        s_event.type = BREATH_EVT_DATA_INVALID;
        snprintf(s_event.reason, sizeof(s_event.reason), "out of bed");
        return;
    }

    /* ── 体动伪影优先 ── */
    s_motion_hist[s_motion_idx] = rf->motion_valid ? (float)rf->body_motion : 0.0f;
    s_motion_idx = (s_motion_idx + 1) % BUF_LEN;
    if (s_motion_cnt < BUF_LEN) s_motion_cnt++;

    float mot_max = 0;
    for (int i = 0; i < s_motion_cnt; i++)
        if (s_motion_hist[i] > mot_max) mot_max = s_motion_hist[i];

    if ((rf->motion_valid && rf->body_motion > 30) || mot_max > 50) {
        s_event.type = BREATH_EVT_MOVEMENT_ARTIFACT;
        s_event.confidence = 70;
        snprintf(s_event.reason, sizeof(s_event.reason),
                 "body motion=%d max10s=%.0f", rf->body_motion, mot_max);
        s_low_amp_sec = 0;
        s_pause_sec = 0;
        return;
    }

    /* ── 呼吸幅度分析 ── */
    if (rf->breath_wave_valid) {
        s_amp_hist[s_amp_idx] = rf->breath_amp;
        s_amp_idx = (s_amp_idx + 1) % BUF_LEN;
        if (s_amp_cnt < BUF_LEN) s_amp_cnt++;

        s_event.breath_amp = rf->breath_amp;
        s_event.quality    = rf->breath_quality;
    }

    /* 基线比较 */
    float bl_amp = 0;
    if (bl && bl->valid && bl->breath_amp > 0) {
        bl_amp = bl->breath_amp;
    } else {
        /* 无基线时用近期均值 */
        float sum = 0; int n = 0;
        for (int i = 0; i < s_amp_cnt; i++) { sum += s_amp_hist[i]; n++; }
        bl_amp = n > 0 ? sum / n : 10.0f;
    }
    s_event.breath_amp_baseline = bl_amp;
    s_event.drop_ratio = (bl_amp > 0) ? 1.0f - (s_event.breath_amp / bl_amp) : 0;

    /* ── 事件判断 ── */
    if (!rf->breath_wave_valid && !rf->breath_valid) {
        s_event.type = BREATH_EVT_UNKNOWN;
        s_event.confidence = 10;
        snprintf(s_event.reason, sizeof(s_event.reason), "no breath data");
        return;
    }

    /* 呼吸接近消失 */
    if (s_event.drop_ratio > 0.8f && s_event.breath_amp < 3.0f) {
        s_pause_sec++;
        s_low_amp_sec++;
        if (s_pause_sec >= 5) {
            s_low_or_pause_latched = true;
            s_event.type = BREATH_EVT_PAUSE_SUSPECTED;
            s_event.confidence = (uint8_t)(40 + s_pause_sec * 5);
            if (s_event.confidence > 80) s_event.confidence = 80;
            snprintf(s_event.reason, sizeof(s_event.reason),
                     "疑似短时呼吸暂停: amp=%.1f (基线%.1f) 持续%ds",
                     s_event.breath_amp, bl_amp, s_pause_sec);
            return;
        }
    } else {
        s_pause_sec = 0;
    }

    /* 呼吸变浅 */
    if (s_event.drop_ratio > 0.4f) {
        s_low_amp_sec++;
        if (s_low_amp_sec >= 10 && s_pause_sec < 5) {
            s_low_or_pause_latched = true;
            s_event.type = BREATH_EVT_SHALLOW;
            s_event.confidence = (uint8_t)(30 + s_low_amp_sec * 2);
            if (s_event.confidence > 70) s_event.confidence = 70;
            snprintf(s_event.reason, sizeof(s_event.reason),
                     "疑似呼吸变浅: amp=%.1f (基线%.1f) 持续%ds",
                     s_event.breath_amp, bl_amp, s_low_amp_sec);
            return;
        }
    } else {
        s_low_amp_sec = 0;
        /* 之前出现过低幅/暂停，随后幅度明显高于基线 → recovery */
        if (s_low_or_pause_latched && s_event.breath_amp > bl_amp * 1.3f) {
            s_low_or_pause_latched = false;
            s_event.type = BREATH_EVT_RECOVERY;
            s_event.confidence = 60;
            snprintf(s_event.reason, sizeof(s_event.reason),
                     "疑似恢复性大呼吸: amp=%.1f > 基线%.1f×1.3",
                     s_event.breath_amp, bl_amp);
            return;
        }
    }

    /* 呼吸不规则 */{
        float bpm_var = 0;
        if (rf->breath_valid) {
            s_bpm_hist[s_bpm_idx] = rf->breath_bpm;
            s_bpm_idx = (s_bpm_idx + 1) % BUF_LEN;
            if (s_bpm_cnt < BUF_LEN) s_bpm_cnt++;
            float sum = 0; int n = 0;
            for (int i = 0; i < s_bpm_cnt; i++) { sum += s_bpm_hist[i]; n++; }
            float mean = n > 0 ? sum / n : 0;
            float var = 0;
            for (int i = 0; i < s_bpm_cnt; i++)
                var += (s_bpm_hist[i] - mean) * (s_bpm_hist[i] - mean);
            bpm_var = n > 0 ? sqrtf(var / n) : 0;
        }
        if (bpm_var > 5.0f && s_amp_cnt >= 5) {
            s_event.type = BREATH_EVT_IRREGULAR;
            s_event.confidence = 50;
            snprintf(s_event.reason, sizeof(s_event.reason),
                     "疑似呼吸不规则: bpm_var=%.1f", bpm_var);
            return;
        }
    }

    /* 默认稳定 */
    s_event.type = BREATH_EVT_STABLE;
    s_event.confidence = 60;
    snprintf(s_event.reason, sizeof(s_event.reason), "呼吸稳定");
}

const breath_event_t *radar_breath_event_get(void) { return &s_event; }
