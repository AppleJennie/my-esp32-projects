#include "r60abd1_adapter.h"
#include "sleep_algorithm.h"
#include "string.h"
#include <math.h>

void r60abd1_adapter_convert(const sleep_radar_data_t *r, radar_feature_t *out)
{
    if (!r || !out) return;
    memset(out, 0, sizeof(radar_feature_t));

    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    out->timestamp_ms = now;
    out->radar_connected = r->radar_connected;

    /* presence */
    if ((now - r->last_presence_ms) <= 10000) {
        out->presence_valid = true;
        out->presence = r->presence;
    }
    /* inferred presence fallback */
    bool inf = ((now - r->last_distance_ms) <= 5000 && r->distance_cm > 0 && r->distance_cm < 300)
            || ((now - r->last_heart_rate_ms) <= 5000 && r->heart_rate > 0)
            || ((now - r->last_breath_rate_ms) <= 5000 && r->breath_rate > 0)
            || ((now - r->last_body_motion_ms) <= 5000 && r->body_motion > 0);
    out->presence_inferred = inf;

    /* bed */
    if ((now - r->last_inbed_update_ms) <= 10000) {
        out->bed_valid = true;
        out->in_bed = r->in_bed;
    }

    /* breath */
    if ((now - r->last_breath_rate_ms) <= 5000) {
        out->breath_valid = true;
        out->breath_bpm = r->breath_rate;
        out->breath_status = r->breath_status;
    }

    /* heart */
    if ((now - r->last_heart_rate_ms) <= 5000 && r->heart_rate > 0) {
        out->heart_valid = true;
        out->heart_bpm = r->heart_rate;
    }

    /* motion */
    if ((now - r->last_body_motion_ms) <= 3000) {
        out->motion_valid = true;
        out->body_motion = r->body_motion;
        out->activity_state = r->motion_state;
    }

    /* position */
    if ((now - r->last_distance_ms) <= 5000) {
        out->position_valid = true;
        out->distance_cm = r->distance_cm;
        out->x_cm = r->target_x_cm;
        out->y_cm = r->target_y_cm;
        out->z_cm = r->target_z_cm;
    }

    /* breath wave: 短窗口 25点(5秒@5Hz) → 当前幅度, 长窗口→基线由sleep_baseline负责 */
    if ((now - r->last_breath_wave_ms) <= 2000 && r->breath_wave_count > 0) {
        out->breath_wave_valid = true;
        for (int i = 0; i < 5; i++) {
            int idx = (r->breath_wave_idx - 5 + i + BREATH_WAVE_BUF_SIZE) % BREATH_WAVE_BUF_SIZE;
            out->breath_wave[i] = (r->breath_wave_count >= 5) ? r->breath_wave_buf[idx] : 0;
        }
        /* 短窗口峰峰值: 最近50点(10秒) */
        int lookback = 50;
        if (r->breath_wave_count < lookback) lookback = r->breath_wave_count;
        uint8_t mn = 255, mx = 0;
        for (int i = 0; i < lookback; i++) {
            int idx = (r->breath_wave_idx - 1 - i + BREATH_WAVE_BUF_SIZE) % BREATH_WAVE_BUF_SIZE;
            uint8_t v = r->breath_wave_buf[idx];
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }
        out->breath_amp = (float)(mx - mn);
        out->breath_quality = (mx > mn) ? 0.8f : 0.2f;

        /* breath_cv: 从最近 150 点(30秒)检测呼吸周期变异 */
        #define CV_WINDOW  100   /* 20秒@5Hz, 够3+周期 */
        #define MIN_PEAK_DIST 9  /* 1.8s → 33次/分上限 */
        int cv_n = (r->breath_wave_count < CV_WINDOW) ? r->breath_wave_count : CV_WINDOW;
        int8_t real[CV_WINDOW];
        for (int i = 0; i < cv_n; i++) {
            int idx = (r->breath_wave_idx - 1 - i + BREATH_WAVE_BUF_SIZE) % BREATH_WAVE_BUF_SIZE;
            real[cv_n - 1 - i] = (int8_t)(r->breath_wave_buf[idx] - 128);  /* 去偏移, 时间正序 */
        }
        /* 峰值检测 */
        int peaks[20], p_cnt = 0;
        int last_peak = -MIN_PEAK_DIST;
        for (int i = 1; i < cv_n - 1 && p_cnt < 20; i++) {
            if (real[i] > 0 && real[i] > real[i-1] && real[i] >= real[i+1]) {
                if (i - last_peak >= MIN_PEAK_DIST) {
                    peaks[p_cnt++] = i;
                    last_peak = i;
                }
            }
        }
        /* 计算周期变异系数 */
        float cv_val = 0.0f;
        if (p_cnt >= 3) {  /* 至少 3 个峰 → 2 个周期 */
            float intervals[19], sum = 0;
            for (int i = 1; i < p_cnt; i++) {
                intervals[i-1] = (float)(peaks[i] - peaks[i-1]) / 5.0f;  /* 采样→秒 */
                sum += intervals[i-1];
            }
            float mean = sum / (p_cnt - 1);
            float var = 0;
            for (int i = 0; i < p_cnt - 1; i++) {
                float d = intervals[i] - mean;
                var += d * d;
            }
            var /= (p_cnt - 1);
            cv_val = (mean > 0.1f) ? (sqrtf(var) / mean) : 0.0f;
        }
        out->breath_cv = (cv_val > 2.0f) ? 2.0f : cv_val;  /* clamp */
        #undef CV_WINDOW
        #undef MIN_PEAK_DIST
    }

    /* heart wave */
    if ((now - r->last_heart_wave_ms) <= 2000 && r->heart_wave_count > 0) {
        out->heart_wave_valid = true;
        for (int i = 0; i < 5; i++) {
            int idx = (r->heart_wave_idx - 5 + i + HEART_WAVE_BUF_SIZE) % HEART_WAVE_BUF_SIZE;
            out->heart_wave[i] = (r->heart_wave_count >= 5) ? r->heart_wave_buf[idx] : 0;
        }
        /* 短窗口峰峰值: 最近50点(10秒) */
        int h_lookback = 50;
        if (r->heart_wave_count < h_lookback) h_lookback = r->heart_wave_count;
        uint8_t mn = 255, mx = 0;
        for (int i = 0; i < h_lookback; i++) {
            int idx = (r->heart_wave_idx - 1 - i + HEART_WAVE_BUF_SIZE) % HEART_WAVE_BUF_SIZE;
            uint8_t v = r->heart_wave_buf[idx];
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }
        out->heart_amp = (float)(mx - mn);
        out->heart_quality = (mx > mn) ? 0.8f : 0.2f;
    }
}
