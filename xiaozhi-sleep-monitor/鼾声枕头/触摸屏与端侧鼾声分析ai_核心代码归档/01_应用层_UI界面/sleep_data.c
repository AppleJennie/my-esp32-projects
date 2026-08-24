#include "sleep_data.h"
#include "esp_timer.h"
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

SleepData_t g_sleep_data;

#define DATA_TIMEOUT_MS  10000

void sleep_data_init(void)
{
    /* Life signs */
    g_sleep_data.spo2 = 96;
    g_sleep_data.heart_rate = 68;
    g_sleep_data.snore_db = 0;
    g_sleep_data.breath_rate = 15;
    g_sleep_data.sleep_score = 0;
    g_sleep_data.movement_level = 0;
    g_sleep_data.in_bed = false;

    /* Counters */
    g_sleep_data.apnea_count = 0;
    g_sleep_data.snore_count = 0;
    g_sleep_data.turn_over_count = 0;
    g_sleep_data.min_spo2 = 99;
    g_sleep_data.max_snore_db = 0;

    /* BLE watch / SpO2 source */
    g_sleep_data.watch_online = false;
    g_sleep_data.watch_spo2_valid = false;
    g_sleep_data.watch_spo2 = 0;
    g_sleep_data.watch_spo2_is_reference = false;
    g_sleep_data.watch_hr_valid = false;
    g_sleep_data.watch_hr = 0;
    g_sleep_data.watch_step = 0;
    g_sleep_data.watch_posture = 0;
    g_sleep_data.last_watch_update_ms = 0;
    g_sleep_data.spo2_from_watch = false;

    /* Advanced respiratory event metrics */
    g_sleep_data.ahi = 0.0f;
    g_sleep_data.t90_percent = 0.0f;
    g_sleep_data.delta_hr = 0;
    g_sleep_data.apnea_duration_sec = 0;
    g_sleep_data.airflow_reduction_percent = 0;
    g_sleep_data.spo2_drop_percent = 0;
    g_sleep_data.supine_event_percent = 0;
    g_sleep_data.apnea_active = false;

    /* Snore audio analyzer */
    g_sleep_data.snore_type = 0;
    g_sleep_data.snore_type_confidence = 0;
    g_sleep_data.spectral_centroid_hz = 0;
    g_sleep_data.low_freq_ratio_x100 = 0;
    g_sleep_data.harmonic_ratio_x100 = 0;
    g_sleep_data.airflow_sound_present = 0;
    g_sleep_data.recovery_breath_sound = 0;
    memset(g_sleep_data.snore_type_count, 0, sizeof(g_sleep_data.snore_type_count));
    g_sleep_data.nasal_snore_ms = 0;
    g_sleep_data.throat_snore_ms = 0;
    g_sleep_data.mouth_snore_ms = 0;
    g_sleep_data.mixed_snore_ms = 0;

    /* MAIN report fields */
    g_sleep_data.event_id = 0;
    g_sleep_data.event_confidence = 0;
    g_sleep_data.breath_amp = 0;
    g_sleep_data.baseline_ok = 0;
    g_sleep_data.baseline_progress = 0;
    g_sleep_data.distance_cm = 0;
    g_sleep_data.hypopnea_count = 0;
    g_sleep_data.snore_resp_score = 0;
    g_sleep_data.system_status = 0;
    g_sleep_data.main_online = false;
    g_sleep_data.has_main_ext = false;

    /* Environment */
    g_sleep_data.temperature = 26.5f;
    g_sleep_data.humidity = 58.0f;
    g_sleep_data.comfort = COMFORT_GOOD;

    /* State */
    g_sleep_data.posture = POSTURE_SUPINE;
    g_sleep_data.risk_level = RISK_NORMAL;
    g_sleep_data.sleep_stage = SLEEP_STAGE_AWAKE;
    g_sleep_data.system_state = SYS_STATE_STANDBY;
    g_sleep_data.monitor_duration_sec = 0;

    /* Sensor status — all offline until first update */
    g_sleep_data.sensor.radar_online = false;
    g_sleep_data.sensor.mic_online = false;
    g_sleep_data.sensor.sd_online = false;
    g_sleep_data.sensor.wifi_connected = false;
    g_sleep_data.sensor.last_radar_update_ms = 0;
    g_sleep_data.sensor.last_mic_update_ms = 0;
    g_sleep_data.sensor.last_env_update_ms = 0;

    g_sleep_data.data_timestamp_ms = 0;
}

bool sleep_data_is_timeout(uint32_t last_update_ms)
{
    if (last_update_ms == 0) return true;  /* never updated */
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    return (now - last_update_ms) > DATA_TIMEOUT_MS;
}

void sleep_data_mock_update(void)
{
    (void)0;
}

/* BLE手表数据更新 — 兼容旧接口。推荐由 watch_ble_client_get_latest() → adapter 统一融合。 */
void sleep_data_update_from_watch(int temp, int humi, int hr, int spo2, int step, int posture)
{
    (void)temp; (void)humi;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    g_sleep_data.watch_online = true;
    g_sleep_data.last_watch_update_ms = now;

    if (spo2 >= 50 && spo2 <= 100) {
        g_sleep_data.watch_spo2_valid = true;
        g_sleep_data.watch_spo2 = spo2;
        g_sleep_data.spo2 = spo2;
        g_sleep_data.spo2_from_watch = true;
        if (g_sleep_data.min_spo2 <= 0 || spo2 < g_sleep_data.min_spo2) {
            g_sleep_data.min_spo2 = spo2;
        }
    }
    if (hr >= 30 && hr <= 220) {
        g_sleep_data.watch_hr_valid = true;
        g_sleep_data.watch_hr = hr;
    }
    g_sleep_data.watch_step = step;
    g_sleep_data.watch_posture = posture;
}
