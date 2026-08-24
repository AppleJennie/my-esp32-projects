/**
 * snore_event_detector.c — 鼾声 episode 合并 + 短时统计
 */
#include "snore_event_detector.h"
#include <string.h>

/* ── 状态 ── */
static snore_event_state_t s_evt;
static snore_episode_t     s_cur_ep;

/* 滑动窗口：最近 60 帧的 episode 记录 */
#define HIST_FRAMES 120  /* micro 4s 一帧 → 120 × 4 = 480s 历史 */
static struct {
    uint32_t ts;
    bool     snoring;
    uint8_t  score;
} s_hist[HIST_FRAMES];
static int s_hist_idx = 0;
static int s_hist_cnt = 0;

/* 累计统计 */
static uint32_t s_episode_total = 0;
static uint32_t s_longest_ms = 0;
static snore_stats_t s_stats;

void snore_event_detector_init(void)
{
    memset(&s_evt, 0, sizeof(s_evt));
    memset(&s_cur_ep, 0, sizeof(s_cur_ep));
    memset(&s_stats, 0, sizeof(s_stats));
    memset(s_hist, 0, sizeof(s_hist));
    s_hist_idx = s_hist_cnt = 0;
    s_episode_total = 0;
    s_longest_ms = 0;
}

void snore_event_detector_feed(const audio_feature_t *af, uint32_t now_ms)
{
    if (!af) return;

    /* ── 滑动窗口记录 ── */
    s_hist[s_hist_idx].ts      = now_ms;
    s_hist[s_hist_idx].snoring  = af->is_snoring;
    s_hist[s_hist_idx].score    = (uint8_t)(af->snore_prob * 255.0f);
    s_hist_idx = (s_hist_idx + 1) % HIST_FRAMES;
    if (s_hist_cnt < HIST_FRAMES) s_hist_cnt++;

    /* ── Episode 管理 ── */
    if (af->is_snoring) {
        if (!s_evt.active) {
            /* 新 episode 开始 */
            s_evt.active = true;
            s_evt.start_ms = now_ms;
            s_evt.total_snore_ms = 0;
            memset(&s_cur_ep, 0, sizeof(s_cur_ep));
            s_cur_ep.start_ms = now_ms;
        }
        s_evt.last_update_ms = now_ms;
        s_evt.total_snore_ms += 4000;  /* micro 推理周期 */

        /* 更新当前 episode 统计 */
        s_cur_ep.end_ms = now_ms;
        s_cur_ep.duration_ms = now_ms - s_cur_ep.start_ms;
        float sc = af->snore_prob * 255.0f;
        if ((uint8_t)sc > s_cur_ep.max_snore_score) s_cur_ep.max_snore_score = (uint8_t)sc;
        /* 累计平均 */
        int n = (s_cur_ep.duration_ms / 4000) + 1;
        s_cur_ep.avg_snore_score = (uint8_t)(
            ((float)s_cur_ep.avg_snore_score * (float)(n - 1) + sc) / (float)n);
        s_cur_ep.avg_rms       = (s_cur_ep.avg_rms       * (n - 1) + af->rms_energy) / n;
        s_cur_ep.avg_zcr       = (s_cur_ep.avg_zcr       * (n - 1) + af->zcr) / n;
        s_cur_ep.avg_low_freq  = (s_cur_ep.avg_low_freq  * (n - 1) + af->low_freq_ratio) / n;
        s_cur_ep.avg_harmonic  = (s_cur_ep.avg_harmonic  * (n - 1) + af->harmonic_ratio) / n;

        /* 最长 episode */
        if (s_cur_ep.duration_ms > s_longest_ms)
            s_longest_ms = s_cur_ep.duration_ms;
    } else {
        if (s_evt.active) {
            /* episode 结束 */
            s_evt.active = false;
            s_episode_total++;
        }
    }

    /* ── 短时统计：遍历滑动窗口 ── */
    uint32_t min1_count = 0, min1_dur = 0;
    uint32_t min10_dur = 0;
    uint32_t min1_snore_start = 0;
    bool     in_snore = false;

    for (int i = 0; i < s_hist_cnt; i++) {
        int idx = (s_hist_idx - 1 - i + HIST_FRAMES) % HIST_FRAMES;
        uint32_t age = now_ms - s_hist[idx].ts;
        if (age > 600000) break;  /* 只看 10 分钟 */

        if (age <= 60000 && s_hist[idx].snoring) {
            min1_dur += 4000;
        }
        if (age <= 600000 && s_hist[idx].snoring) {
            min10_dur += 4000;
        }
    }

    /* episode 计数（1 分钟内） */
    for (int i = 0; i < s_hist_cnt; i++) {
        int idx = (s_hist_idx - 1 - i + HIST_FRAMES) % HIST_FRAMES;
        uint32_t age = now_ms - s_hist[idx].ts;
        if (age > 60000) break;
        if (s_hist[idx].snoring && !in_snore) {
            min1_count++;
            in_snore = true;
        } else if (!s_hist[idx].snoring) {
            in_snore = false;
        }
    }

    s_stats.last_minute_count       = min1_count;
    s_stats.last_minute_duration_ms = min1_dur;
    s_stats.last_10min_duration_ms  = min10_dur;
    s_stats.snore_burden_pct        = (min10_dur > 0)
        ? (float)min10_dur / 600000.0f * 100.0f : 0.0f;
    s_stats.longest_episode_ms      = s_longest_ms;
    s_stats.episode_count_total     = s_episode_total;
}

const snore_episode_t *snore_event_current_episode(void) { return &s_cur_ep; }
bool snore_event_is_active(void) { return s_evt.active; }
uint32_t snore_event_active_duration_ms(uint32_t now_ms) {
    return s_evt.active ? (now_ms - s_evt.start_ms) : 0;
}
void snore_event_get_stats(snore_stats_t *out) {
    if (out) memcpy(out, &s_stats, sizeof(s_stats));
}
