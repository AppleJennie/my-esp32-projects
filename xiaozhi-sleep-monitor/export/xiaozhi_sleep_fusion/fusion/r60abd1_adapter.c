#include "r60abd1_adapter.h"
#include "sleep_algorithm.h"
#include "string.h"

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

    /* breath wave */
    if ((now - r->last_breath_wave_ms) <= 2000 && r->breath_wave_count > 0) {
        out->breath_wave_valid = true;
        for (int i = 0; i < 5; i++) {
            int idx = (r->breath_wave_idx - 5 + i + BREATH_WAVE_BUF_SIZE) % BREATH_WAVE_BUF_SIZE;
            out->breath_wave[i] = (r->breath_wave_count >= 5) ? r->breath_wave_buf[idx] : 0;
        }
        uint8_t mn = 255, mx = 0;
        for (int i = 0; i < r->breath_wave_count && i < BREATH_WAVE_BUF_SIZE; i++) {
            if (r->breath_wave_buf[i] < mn) mn = r->breath_wave_buf[i];
            if (r->breath_wave_buf[i] > mx) mx = r->breath_wave_buf[i];
        }
        out->breath_amp = (float)(mx - mn);
        out->breath_quality = (mx > mn) ? 0.8f : 0.2f;
    }

    /* heart wave */
    if ((now - r->last_heart_wave_ms) <= 2000 && r->heart_wave_count > 0) {
        out->heart_wave_valid = true;
        for (int i = 0; i < 5; i++) {
            int idx = (r->heart_wave_idx - 5 + i + HEART_WAVE_BUF_SIZE) % HEART_WAVE_BUF_SIZE;
            out->heart_wave[i] = (r->heart_wave_count >= 5) ? r->heart_wave_buf[idx] : 0;
        }
        uint8_t mn = 255, mx = 0;
        for (int i = 0; i < r->heart_wave_count && i < HEART_WAVE_BUF_SIZE; i++) {
            if (r->heart_wave_buf[i] < mn) mn = r->heart_wave_buf[i];
            if (r->heart_wave_buf[i] > mx) mx = r->heart_wave_buf[i];
        }
        out->heart_amp = (float)(mx - mn);
        out->heart_quality = (mx > mn) ? 0.8f : 0.2f;
    }
}
