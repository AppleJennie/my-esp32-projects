/**
 * snore_event_detector.c — 鼾声 episode 合并 + 短时统计
 */
#include "snore_event_detector.h"
#include <string.h>

#define HIST_FRAMES 180              /* 4s 一帧约 12 分钟历史，足够计算最近 10 分钟 */
#define DEFAULT_FRAME_MS 4000
#define MIN_EPISODE_MS 4000          /* 少于一个推理周期的抖动不计为完整 episode */

static snore_event_state_t s_evt;
static snore_episode_t     s_cur_ep;
static snore_stats_t       s_stats;

static struct {
    uint32_t ts;
    uint32_t dur_ms;
    bool     snoring;
    uint8_t  score;
} s_hist[HIST_FRAMES];
static int s_hist_idx = 0;
static int s_hist_cnt = 0;

static uint32_t s_first_ts = 0;
static uint32_t s_last_frame_ts = 0;
static uint32_t s_episode_total = 0;
static uint32_t s_total_snore_ms = 0;
static uint32_t s_longest_ms = 0;

static uint32_t clamp_frame_delta(uint32_t d)
{
    if (d == 0) return 0;
    if (d < 1000) return 1000;
    if (d > 8000) return DEFAULT_FRAME_MS;
    return d;
}

void snore_event_detector_init(void)
{
    memset(&s_evt, 0, sizeof(s_evt));
    memset(&s_cur_ep, 0, sizeof(s_cur_ep));
    memset(&s_stats, 0, sizeof(s_stats));
    memset(s_hist, 0, sizeof(s_hist));
    s_hist_idx = s_hist_cnt = 0;
    s_first_ts = 0;
    s_last_frame_ts = 0;
    s_episode_total = 0;
    s_total_snore_ms = 0;
    s_longest_ms = 0;
}

void snore_event_detector_feed(const audio_feature_t *af, uint32_t now_ms)
{
    if (!af || !af->audio_valid) return;

    uint32_t frame_ts = af->timestamp_ms ? af->timestamp_ms : now_ms;

    /* audio_pipeline 通常 4 秒才更新一次；health tick 可能每秒调用。
     * 如果同一帧被重复喂入，直接忽略，避免总时长被重复累计。 */
    if (s_last_frame_ts != 0 && frame_ts == s_last_frame_ts) {
        return;
    }

    uint32_t frame_ms = DEFAULT_FRAME_MS;
    if (s_last_frame_ts != 0 && frame_ts > s_last_frame_ts) {
        frame_ms = clamp_frame_delta(frame_ts - s_last_frame_ts);
    }
    if (s_first_ts == 0) s_first_ts = frame_ts;
    s_last_frame_ts = frame_ts;

    uint8_t score = (uint8_t)(af->snore_prob * 255.0f);

    s_hist[s_hist_idx].ts = frame_ts;
    s_hist[s_hist_idx].dur_ms = frame_ms;
    s_hist[s_hist_idx].snoring = af->is_snoring;
    s_hist[s_hist_idx].score = score;
    s_hist_idx = (s_hist_idx + 1) % HIST_FRAMES;
    if (s_hist_cnt < HIST_FRAMES) s_hist_cnt++;

    if (af->is_snoring) {
        if (!s_evt.active) {
            s_evt.active = true;
            s_evt.start_ms = frame_ts;
            s_evt.total_snore_ms = 0;
            memset(&s_cur_ep, 0, sizeof(s_cur_ep));
            s_cur_ep.start_ms = frame_ts;
        }
        s_evt.last_update_ms = frame_ts;
        s_evt.total_snore_ms += frame_ms;
        s_total_snore_ms += frame_ms;

        s_cur_ep.end_ms = frame_ts;
        s_cur_ep.duration_ms = s_evt.total_snore_ms;
        if (score > s_cur_ep.max_snore_score) s_cur_ep.max_snore_score = score;

        uint32_t n = s_evt.total_snore_ms / DEFAULT_FRAME_MS;
        if (n == 0) n = 1;
        s_cur_ep.avg_snore_score = (uint8_t)(((float)s_cur_ep.avg_snore_score * (float)(n - 1) + (float)score) / (float)n);
        s_cur_ep.avg_rms       = (s_cur_ep.avg_rms       * (float)(n - 1) + af->rms_energy) / (float)n;
        s_cur_ep.avg_zcr       = (s_cur_ep.avg_zcr       * (float)(n - 1) + af->zcr) / (float)n;
        s_cur_ep.avg_low_freq  = (s_cur_ep.avg_low_freq  * (float)(n - 1) + af->low_freq_ratio) / (float)n;
        s_cur_ep.avg_harmonic  = (s_cur_ep.avg_harmonic  * (float)(n - 1) + af->harmonic_ratio) / (float)n;

        if (s_cur_ep.duration_ms > s_longest_ms) s_longest_ms = s_cur_ep.duration_ms;
    } else {
        if (s_evt.active) {
            if (s_evt.total_snore_ms >= MIN_EPISODE_MS) {
                s_episode_total++;
            }
            s_evt.active = false;
        }
    }

    uint32_t min1_count = 0, min1_dur = 0, min10_dur = 0;
    bool in_snore = false;

    for (int i = 0; i < s_hist_cnt; i++) {
        int idx = (s_hist_idx - 1 - i + HIST_FRAMES) % HIST_FRAMES;
        uint32_t age = (frame_ts >= s_hist[idx].ts) ? (frame_ts - s_hist[idx].ts) : 0;
        if (age > 600000) break;

        if (s_hist[idx].snoring) {
            if (age <= 600000) min10_dur += s_hist[idx].dur_ms;
            if (age <= 60000)  min1_dur += s_hist[idx].dur_ms;
        }

        if (age <= 60000) {
            if (s_hist[idx].snoring && !in_snore) {
                min1_count++;
                in_snore = true;
            } else if (!s_hist[idx].snoring) {
                in_snore = false;
            }
        }
    }

    uint32_t observed_ms = (frame_ts > s_first_ts) ? (frame_ts - s_first_ts + frame_ms) : frame_ms;
    uint32_t burden_den = observed_ms < 600000 ? observed_ms : 600000;
    if (burden_den < frame_ms) burden_den = frame_ms;

    s_stats.last_minute_count       = min1_count;
    s_stats.last_minute_duration_ms = min1_dur;
    s_stats.last_10min_duration_ms  = min10_dur;
    s_stats.total_duration_ms       = s_total_snore_ms;
    s_stats.current_episode_ms      = s_evt.active ? s_evt.total_snore_ms : 0;
    s_stats.snore_burden_pct        = (float)min10_dur * 100.0f / (float)burden_den;
    s_stats.snore_density_pct       = (observed_ms > 0) ? ((float)s_total_snore_ms * 100.0f / (float)observed_ms) : 0.0f;
    s_stats.longest_episode_ms      = s_longest_ms;
    s_stats.episode_count_total     = s_episode_total + (s_evt.active ? 1 : 0);
}

const snore_episode_t *snore_event_current_episode(void) { return &s_cur_ep; }
bool snore_event_is_active(void) { return s_evt.active; }
uint32_t snore_event_active_duration_ms(uint32_t now_ms) { (void)now_ms; return s_evt.active ? s_evt.total_snore_ms : 0; }
void snore_event_get_stats(snore_stats_t *out) { if (out) memcpy(out, &s_stats, sizeof(s_stats)); }
