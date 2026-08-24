/**
 * sleep_baseline.c — 个人基线采集（120 秒窗口，连续 30 秒不稳定才重置）
 */
#include "sleep_baseline.h"
#include "esp_log.h"
#include "string.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "BASELINE";

#define BL_MIN_DURATION_MS   100      /* 调试: 100ms, 正式: 120000 */
#define BL_MOTION_MAX        15      /* 体动 <15 才采基线 */
#define BL_MIN_SAMPLES       30
#define BL_RESET_STREAK      30      /* 连续 30 秒不稳定才重置 */

static baseline_t s_bl;

/* 运行累计 */
static uint32_t s_bl_start_ms    = 0;
static uint32_t s_bl_last_ok_ms  = 0;
static float    s_bl_breath_amp_sum = 0;
static float    s_bl_breath_bpm_sum = 0;
static float    s_bl_heart_bpm_sum  = 0;
static float    s_bl_motion_sum     = 0;
static float    s_bl_energy_sum     = 0;
static int      s_bl_count          = 0;
static int      s_unstable_streak   = 0;

void sleep_baseline_init(void)
{
    memset(&s_bl, 0, sizeof(s_bl));
    s_bl_start_ms    = 0;
    s_bl_last_ok_ms  = 0;
    s_bl_count       = 0;
    s_unstable_streak = 0;
    s_bl_breath_amp_sum = s_bl_breath_bpm_sum = s_bl_heart_bpm_sum = 0;
    s_bl_motion_sum = s_bl_energy_sum = 0;
    ESP_LOGI(TAG, "Baseline module init");
}

void sleep_baseline_reset(void)
{
    sleep_baseline_init();
    ESP_LOGI(TAG, "Baseline reset");
}

void sleep_baseline_feed(const radar_feature_t *r, const audio_feature_t *a)
{
    if (!r) return;
    if (s_bl.valid) return;  /* 基线已就绪，不再更新 */

    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    /* 条件判断 */
    bool presence = r->presence_valid ? r->presence : r->presence_inferred;
    bool in_bed   = r->bed_valid && r->in_bed;
    bool breath_ok = r->breath_valid || r->breath_wave_valid;
    bool heart_ok  = r->heart_valid;
    bool still     = r->body_motion < BL_MOTION_MAX;

    /* 调试: 放宽基线条件, 不再要求必须有人在床 */
    bool conditions_met = breath_ok || heart_ok;  /* 正式: presence && in_bed && breath_ok && heart_ok && still */
    still = still;  /* unused */

    if (!conditions_met) {
        return;
    }

    /* 条件满足：重置不稳定计数 */
    s_unstable_streak = 0;

    if (s_bl_start_ms == 0) {
        s_bl_start_ms = now;
        ESP_LOGI(TAG, "Baseline collection started");
    }

    /* 累积数据 */
    if (r->breath_wave_valid) s_bl_breath_amp_sum += r->breath_amp;
    if (r->breath_valid)      s_bl_breath_bpm_sum += r->breath_bpm;
    if (r->heart_valid)       s_bl_heart_bpm_sum  += r->heart_bpm;
    s_bl_motion_sum += (float)r->body_motion;
    if (a && a->audio_valid)  s_bl_energy_sum += a->rms_energy;
    s_bl_count++;
    s_bl_last_ok_ms = now;

    uint32_t elapsed = now - s_bl_start_ms;
    s_bl.progress_seconds = (int)(elapsed / 1000);
    if (s_bl.progress_seconds > 120) s_bl.progress_seconds = 120;

    /* 达到最小收集时长 */
    if (elapsed >= BL_MIN_DURATION_MS && s_bl_count >= BL_MIN_SAMPLES) {
        s_bl.breath_amp  = s_bl_breath_amp_sum / (float)s_bl_count;
        s_bl.breath_bpm  = s_bl_breath_bpm_sum / (float)s_bl_count;
        s_bl.heart_bpm   = s_bl_heart_bpm_sum  / (float)s_bl_count;
        s_bl.body_motion = s_bl_motion_sum      / (float)s_bl_count;
        s_bl.audio_energy = s_bl_energy_sum     / (float)s_bl_count;
        s_bl.noise_floor  = s_bl.audio_energy * 0.3f;
        s_bl.valid        = true;
        s_bl.collected_ms = elapsed;
        s_bl.sample_count = s_bl_count;
        s_bl.quality      = 0.8f;

        if (r->heart_valid) s_bl.quality += 0.1f;
        if (a && a->audio_valid) s_bl.quality += 0.1f;
        if (s_bl.quality > 1.0f) s_bl.quality = 1.0f;

        ESP_LOGI(TAG, "Baseline READY: bw_amp=%.1f br=%.1f hr=%.0f motion=%.1f "
                 "audio=%.3f quality=%.2f samples=%d time=%lums",
                 s_bl.breath_amp, s_bl.breath_bpm, s_bl.heart_bpm, s_bl.body_motion,
                 s_bl.audio_energy, s_bl.quality, s_bl.sample_count, elapsed);
    }
}

bool sleep_baseline_is_ready(void)  { return s_bl.valid; }
const baseline_t *sleep_baseline_get(void) { return &s_bl; }
int sleep_baseline_progress_sec(void) { return s_bl.progress_seconds; }
