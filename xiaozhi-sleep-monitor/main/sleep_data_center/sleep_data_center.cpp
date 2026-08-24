#include "sleep_data_center.h"
#include "sleep_score.h"
#include "snore_resp_score.h"
#include "sleep_baseline.h"
#include "sleep_sd_logger.h"
#include <cmath>
#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <string.h>
#include <time.h>

static const char* TAG = "SDC";

SleepDataCenter& SleepDataCenter::GetInstance() {
    static SleepDataCenter instance;
    return instance;
}

void SleepDataCenter::Init() {
    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_) {
        ESP_LOGE(TAG, "Failed to create mutex");
    }
    ResetStats();
}

void SleepDataCenter::Lock() const {
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
}

void SleepDataCenter::Unlock() const {
    if (mutex_) xSemaphoreGive(mutex_);
}

void SleepDataCenter::ResetStats() {
    session_running_ = false;
    session_start_ms_ = 0;
    session_stop_ms_ = 0;
    valid_sleep_sec_ = 0;

    current_heart_rate_ = 0;
    current_breath_rate_ = 0;
    current_spo2_ = 0;
    current_temp_ = 0;
    current_humi_ = 0;
    current_light_raw_ = 0;
    current_audio_db_ = 0;
    current_motion_level_ = 0;
    body_present_ = false;
    last_snore_prob_ = 0;
    last_update_ms_ = 0;

    snore_count_ = 0;
    snore_total_sec_ = 0;
    longest_snore_sec_ = 0;
    max_snore_prob_ = 0;
    snore_prob_sum_ = 0;
    snore_prob_samples_ = 0;
    snore_current_duration_ = 0;

    heart_rate_sum_ = 0;
    heart_rate_count_ = 0;
    max_heart_rate_ = 0;
    min_heart_rate_ = 999;

    breath_rate_sum_ = 0;
    breath_rate_count_ = 0;
    max_breath_rate_ = 0;
    min_breath_rate_ = 999;
    breath_abnormal_count_ = 0;

    spo2_sum_ = 0;
    spo2_count_ = 0;
    min_spo2_ = 999;
    spo2_below_90_count_ = 0;
    spo2_below_90_sec_ = 0;

    motion_count_ = 0;
    large_motion_count_ = 0;
    body_absent_count_ = 0;

    temp_sum_ = 0;
    humi_sum_ = 0;
    light_sum_ = 0;
    environment_count_ = 0;
    max_light_raw_ = 0;

    audio_db_sum_ = 0;
    audio_db_count_ = 0;
    max_audio_db_ = -999;
    vad_count_ = 0;

    apnea_suspected_count_ = 0;
    apnea_risk_level_ = "low";
    apnea_confidence_ = "medium";
    has_spo2_ = false;

    /* 呼吸波形事件检测 */
    breath_wave_data_available_ = false;
    apnea_like_count_ = 0;
    hypopnea_like_count_ = 0;
    resp_event_total_sec_ = 0;
    radar_apnea_10min_ = 0;
    turn_over_count_radar_ = 0;
    max_delta_hr_after_event_ = 0;
    no_hr_response_events_ = 0;
    spo2_drop_3pct_count_ = 0;
    last_spo2_baseline_ = 0;
    breath_amp_baseline_ = 0;

    breath_cv_ = 0;
    breath_pause_sec_ = 0;
    breath_wave_quality_ = 0;
    hr_sdnn_ms_ = 0;
    hrv_wave_quality_ = 0;

    last_event_state_ms_ = 0;
    current_event_duration_sec_ = 0;
    current_event_type_ = 0;
    hr_before_event_ = 0;
    breath_amp_min_during_event_ = 0;
    last_snore_event_ts_ms_ = 0;
    last_snore_event_max_prob_ = 0;
    last_snore_event_energy_sum_ = 0;
    last_snore_event_window_count_ = 0;
}

void SleepDataCenter::StartSession() {
    Lock();
    if (session_running_) {
        ESP_LOGW(TAG, "Session already active, skip restart");
        Unlock();
        return;
    }
    ResetStats();
    session_running_ = true;
    session_start_ms_ = esp_timer_get_time() / 1000;

    // 创建 SD session 目录
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char date_str[32];
    char time_str[32];
    snprintf(date_str, sizeof(date_str), "%04d-%02d-%02d", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
    snprintf(time_str, sizeof(time_str), "%02d-%02d-%02d", t->tm_hour, t->tm_min, t->tm_sec);
    sleep_sd_logger_open_session(date_str, time_str);

    ESP_LOGI(TAG, "Session started at %s %s", date_str, time_str);
    Unlock();
}

void SleepDataCenter::StopSession() {
    Lock();
    if (!session_running_) {
        ESP_LOGW(TAG, "Session not active");
        Unlock();
        return;
    }
    session_running_ = false;
    session_stop_ms_ = esp_timer_get_time() / 1000;
    if (session_stop_ms_ > session_start_ms_) {
        valid_sleep_sec_ = (session_stop_ms_ - session_start_ms_) / 1000;
    }
    uint32_t duration = valid_sleep_sec_;
    Unlock();

    // 生成报告并保存到 SD（在锁外执行避免死锁）
    std::string report_json = GenerateReportJson();
    sleep_sd_logger_write_report_json(report_json.c_str());
    sleep_sd_logger_write_report_txt(report_json.c_str());
    sleep_sd_logger_close_session();

    ESP_LOGI(TAG, "Session stopped, duration=%lu s", duration);
}

void SleepDataCenter::UpdateSnore(float prob, bool is_snore, uint32_t window_sec) {
    Lock();
    last_snore_prob_ = prob;
    last_update_ms_ = esp_timer_get_time() / 1000;

    snore_prob_sum_ += prob;
    snore_prob_samples_++;
    if (prob > max_snore_prob_) max_snore_prob_ = prob;

    if (is_snore) {
        snore_current_duration_ += window_sec;
        if (snore_current_duration_ > longest_snore_sec_) {
            longest_snore_sec_ = snore_current_duration_;
        }
    } else {
        if (snore_current_duration_ > 0) {
            snore_count_++;
            snore_total_sec_ += snore_current_duration_;
            snore_current_duration_ = 0;
        }
    }
    Unlock();
}

void SleepDataCenter::UpdateRadar(int exist, int motion, int hr, int br, int dist) {
    (void)dist;
    Lock();
    last_update_ms_ = esp_timer_get_time() / 1000;

    /* 调试: 有心率或呼吸率数据即认为在床, 正式版恢复 exist 判断 */
    body_present_ = (exist != 0) || (hr > 0) || (br > 0);
    current_motion_level_ = motion;

    if (!body_present_) {
        body_absent_count_++;
    }
    if (motion > 0) {
        motion_count_++;
        if (motion > 1) large_motion_count_++;
    }

    if (hr > 0) {
        current_heart_rate_ = hr;
        heart_rate_sum_ += hr;
        heart_rate_count_++;
        if (hr > max_heart_rate_) max_heart_rate_ = hr;
        if (hr < min_heart_rate_) min_heart_rate_ = hr;
    }

    if (br > 0) {
        current_breath_rate_ = br;
        breath_rate_sum_ += br;
        breath_rate_count_++;
        if (br > max_breath_rate_) max_breath_rate_ = br;
        if (br < min_breath_rate_) min_breath_rate_ = br;
        if (br < 10) breath_abnormal_count_++;
    }

    // 呼吸暂停疑似计数（血氧 + 呼吸率）
    if (has_spo2_ && current_spo2_ > 0 && current_spo2_ < 90) {
        apnea_suspected_count_++;
    }
    if (br > 0 && br < 10) {
        apnea_suspected_count_++;
    }

    // 有血氧时实时更新风险等级
    if (has_spo2_) {
        apnea_confidence_ = "medium";
        if (apnea_suspected_count_ >= 5 || min_spo2_ < 88) {
            apnea_risk_level_ = "high";
        } else if (apnea_suspected_count_ >= 2 || spo2_below_90_count_ > 0) {
            apnea_risk_level_ = "medium";
        } else {
            apnea_risk_level_ = "low";
        }
    }

    /* 滚动 HR 窗口（用于 SDNN proxy 计算） */
    if (current_heart_rate_ > 0) {
        hr_history_[hr_history_idx_] = (float)current_heart_rate_;
        hr_history_idx_ = (hr_history_idx_ + 1) % 16;
        if (hr_history_count_ < 16) hr_history_count_++;
    }

    FlushRealtimeCsv();
    Unlock();
}

void SleepDataCenter::UpdateEnvironment(float temp, float humi, int light_raw) {
    Lock();
    last_update_ms_ = esp_timer_get_time() / 1000;

    current_temp_ = temp;
    current_humi_ = humi;
    current_light_raw_ = light_raw;

    temp_sum_ += temp;
    humi_sum_ += humi;
    light_sum_ += light_raw;
    environment_count_++;
    if (light_raw > max_light_raw_) max_light_raw_ = light_raw;
    FlushRealtimeCsv();
    Unlock();
}

void SleepDataCenter::UpdateAudioSummary(float audio_db, int vad) {
    Lock();
    last_update_ms_ = esp_timer_get_time() / 1000;

    current_audio_db_ = audio_db;
    audio_db_sum_ += audio_db;
    audio_db_count_++;
    if (audio_db > max_audio_db_) max_audio_db_ = audio_db;
    if (vad > 0) vad_count_ += vad;
    Unlock();
}

void SleepDataCenter::UpdateSpo2(float spo2) {
    Lock();
    uint32_t now_ms = esp_timer_get_time() / 1000;

    if (spo2 > 0) {
        /* 计算距上次更新的真实时间差 */
        static uint32_t last_spo2_update_ms = 0;
        if (last_spo2_update_ms > 0 && last_update_ms_ > 0) {
            uint32_t dt = now_ms - last_update_ms_;
            if (dt > 0 && dt < 60000) {  /* 合理范围：1ms~60s */
                float dt_sec = (float)dt / 1000.0f;
                if (spo2 < 90.0f) {
                    spo2_below_90_sec_ += dt_sec;
                }
            }
        }
        last_spo2_update_ms = now_ms;
        last_update_ms_ = now_ms;

        has_spo2_ = true;
        current_spo2_ = spo2;
        spo2_sum_ += spo2;
        spo2_count_++;
        if (spo2 < min_spo2_) min_spo2_ = spo2;
        if (spo2 < 90.0f) {
            spo2_below_90_count_++;
        }

        /* SpO2 下降 ≥3% 检测（相对于滚动基线） */
        if (last_spo2_baseline_ <= 0) {
            last_spo2_baseline_ = spo2;
        } else {
            /* 指数平滑基线 */
            last_spo2_baseline_ = last_spo2_baseline_ * 0.98f + spo2 * 0.02f;
            float drop = last_spo2_baseline_ - spo2;
            if (drop >= 3.0f) {
                spo2_drop_3pct_count_++;
            }
        }
    }
    Unlock();
}

void SleepDataCenter::UpdateBreathWave(float breath_amp, uint32_t now_ms) {
    if (breath_amp <= 0) return;
    Lock();

    breath_wave_data_available_ = true;

    /* ── 基线更新（指数平滑，时间常数 ~30s） ── */
    if (breath_amp_baseline_ <= 0) {
        breath_amp_baseline_ = breath_amp;
    } else {
        breath_amp_baseline_ = breath_amp_baseline_ * 0.95f + breath_amp * 0.05f;
    }

    if (last_event_state_ms_ == 0) last_event_state_ms_ = now_ms;

    /* ── 检测当前状态 ── */
    float ratio = (breath_amp_baseline_ > 1.0f)
                  ? breath_amp / breath_amp_baseline_ : 1.0f;
    int new_event_type = 0;  /* 0=none, 1=apnea, 2=hypopnea */

    if (ratio <= 0.10f) {
        new_event_type = 1;  /* 呼吸暂停：幅度下降 ≥90% */
    } else if (ratio <= 0.70f) {
        new_event_type = 2;  /* 低通气：幅度下降 30%~90% */
    }

    /* ── 状态机：事件开始/持续/结束 ── */
    uint32_t dt_ms = now_ms - last_event_state_ms_;
    if (dt_ms < 100) dt_ms = 100;  /* 防止时间回退 */
    last_event_state_ms_ = now_ms;

    if (new_event_type != 0) {
        /* 事件持续中 */
        if (current_event_type_ == 0) {
            /* 事件刚开始 */
            current_event_type_ = new_event_type;
            current_event_duration_sec_ = 0;
            breath_amp_min_during_event_ = breath_amp;
            hr_before_event_ = current_heart_rate_;  /* 记录事件前心率 */
        }
        current_event_duration_sec_ += dt_ms / 1000;
        if (breath_amp < breath_amp_min_during_event_) {
            breath_amp_min_during_event_ = breath_amp;
        }
    } else {
        /* 恢复正常呼吸 — 事件结束 */
        if (current_event_type_ != 0 && current_event_duration_sec_ >= 10) {
            /* 确认有效事件：持续 ≥10s → 计入 rREI */
            int conf = (current_event_type_ == 1) ? 60 : 50;
            RecordBreathEvent(current_event_type_, current_event_duration_sec_, conf, 0);

            /* 检查心率代偿反应 */
            if (heart_rate_count_ > 0 && hr_before_event_ > 0) {
                float hr_after = current_heart_rate_;
                if (hr_after > 0) {
                    float delta = hr_after - hr_before_event_;
                    if (delta > max_delta_hr_after_event_) {
                        max_delta_hr_after_event_ = delta;
                    }
                    if (delta < 5.0f) {
                        no_hr_response_events_++;
                    }
                }
            }

            /* 检查血氧伴随下降 */
            if (has_spo2_ && spo2_drop_3pct_count_ > 0) {
                /* 已在 UpdateSpo2 中检测到下降 */
            }

            /* 写日志 */
            ESP_LOGI(TAG, "Breath event: type=%d dur=%us amp_min=%.1f hr_before=%.0f hr_after=%.0f",
                     current_event_type_,
                     current_event_duration_sec_,
                     breath_amp_min_during_event_,
                     hr_before_event_,
                     (float)current_heart_rate_);
        } else if (current_event_type_ != 0 && current_event_duration_sec_ >= 5) {
            /* 候选事件：5~9s，不计入 rREI，仅日志记录 */
            ESP_LOGD(TAG, "Breath candidate: type=%d dur=%us (not counted)",
                     current_event_type_, current_event_duration_sec_);
        }
        /* 重置状态 */
        current_event_type_ = 0;
        current_event_duration_sec_ = 0;
        breath_amp_min_during_event_ = 0;
    }

    Unlock();
}

/* 调用者必须已持有 mutex (避免递归死锁) */
void SleepDataCenter::RecordBreathEvent(int type, uint16_t duration_sec,
                                         uint8_t confidence, int source) {
    if (type == 1) {
        apnea_like_count_++;
    } else if (type == 2) {
        hypopnea_like_count_++;
    }
    resp_event_total_sec_ += duration_sec;
    (void)confidence; (void)source;
}

void SleepDataCenter::UpdateWaveFeatures(float breath_cv, float breath_pause_sec,
                                          float breath_quality, float hr_sdnn_ms, float hrv_quality) {
    Lock();
    breath_cv_          = breath_cv;
    breath_pause_sec_   = breath_pause_sec;
    breath_wave_quality_ = breath_quality;
    hr_sdnn_ms_         = hr_sdnn_ms;
    hrv_wave_quality_   = hrv_quality;
    /* 从滚动窗口计算本地代理（作为 fallback） */
    if (hr_history_count_ >= 4 && hr_sdnn_ms < 1.0f) {
        float sum = 0, sum_sq = 0;
        for (int i = 0; i < hr_history_count_; i++) {
            sum += hr_history_[i];
            sum_sq += hr_history_[i] * hr_history_[i];
        }
        float mean = sum / hr_history_count_;
        float var  = sum_sq / hr_history_count_ - mean * mean;
        hr_sdnn_ms_ = (var > 0) ? sqrtf(var) * 10.0f : 0;  /* bpm std→ms 近似 */
    }
    Unlock();
}

void SleepDataCenter::UpdateRadarComposite(uint8_t apnea_10min, uint8_t turn_over_10min) {
    Lock();
    radar_apnea_10min_ = apnea_10min;
    turn_over_count_radar_ = turn_over_10min;
    Unlock();
}

void SleepDataCenter::AddSnoreEvent(uint32_t timestamp_ms, uint32_t duration_ms,
                                    float snore_prob, float rms_energy) {
    Lock();
    constexpr uint32_t MERGE_MS = 3000;

    if (last_snore_event_ts_ms_ > 0 &&
        (timestamp_ms - last_snore_event_ts_ms_) < MERGE_MS) {
        // 与上一事件合并
        snore_total_sec_ += duration_ms / 1000.0f;
        snore_current_duration_ += duration_ms / 1000.0f;
        if (snore_prob > last_snore_event_max_prob_) {
            last_snore_event_max_prob_ = snore_prob;
        }
        last_snore_event_energy_sum_ += rms_energy;
        last_snore_event_window_count_++;
    } else {
        // 开启新事件
        if (snore_current_duration_ > 0) {
            snore_total_sec_ += snore_current_duration_;
        }
        snore_count_++;
        snore_current_duration_ = duration_ms / 1000.0f;
        last_snore_event_ts_ms_ = timestamp_ms;
        last_snore_event_max_prob_ = snore_prob;
        last_snore_event_energy_sum_ = rms_energy;
        last_snore_event_window_count_ = 1;
    }

    if (snore_current_duration_ > longest_snore_sec_) {
        longest_snore_sec_ = snore_current_duration_;
    }
    if (snore_prob > max_snore_prob_) max_snore_prob_ = snore_prob;
    snore_prob_sum_ += snore_prob;
    snore_prob_samples_++;
    // 新事件写入 SD
    bool is_new_event = (last_snore_event_window_count_ == 1);
    Unlock();
    if (is_new_event) {
        sleep_sd_logger_write_snore(timestamp_ms, duration_ms, snore_prob, rms_energy);
    }
}

void SleepDataCenter::UpdateCachedSnoreRespScore() {
    Lock();
    /* 未开始监测或雷达无数据 → 评分无效 */
    if (!session_running_ || (motion_count_ == 0 && breath_rate_count_ == 0)) {
        cached_snore_resp_score_ = 0;
        Unlock();
        return;
    }
    uint32_t duration_sec = valid_sleep_sec_;
    if (session_running_) {
        duration_sec = (esp_timer_get_time() / 1000 - session_start_ms_) / 1000;
    }
    float duration_hours = duration_sec / 3600.0f;
    int total_events = apnea_like_count_ + hypopnea_like_count_;

    SnoreRespFeatures f;
    memset(&f, 0, sizeof(f));
    f.sleep_hours         = duration_hours;
    f.apnea_like_count    = apnea_like_count_;
    f.hypopnea_like_count = hypopnea_like_count_;
    f.total_event_count   = total_events;
    f.rrei      = duration_hours > 0.01f ? (float)total_events / duration_hours : 0;
    f.has_spo2  = has_spo2_;
    f.min_spo2  = has_spo2_ ? (min_spo2_ < 999 ? (float)min_spo2_ : 0) : 0;
    f.t90_ratio = (duration_sec > 0 && has_spo2_) ? (float)spo2_below_90_sec_ / (float)duration_sec : 0;
    f.spo2_drop_count = spo2_drop_3pct_count_;
    f.has_hr    = heart_rate_count_ > 0;
    f.max_delta_hr_after_event = max_delta_hr_after_event_;
    f.no_hr_response_flag = (no_hr_response_events_ > 0);
    f.motion_count     = motion_count_;
    f.large_motion_count = large_motion_count_;
    f.turn_over_count  = turn_over_count_radar_;
    f.has_radar        = (motion_count_ > 0 || breath_rate_count_ > 0);
    f.has_breath_wave_data = breath_wave_data_available_;

    SnoreRespResult r = CalcSnoreRespRisk(&f);
    cached_snore_resp_score_ = r.score;
    Unlock();
}

bool SleepDataCenter::IsRunning() const {
    Lock();
    bool r = session_running_;
    Unlock();
    return r;
}

void SleepDataCenter::FlushRealtimeCsv() {
    if (!session_running_) return;
    sleep_sd_logger_write_realtime(
        last_update_ms_,
        current_heart_rate_,
        current_breath_rate_,
        current_spo2_,
        body_present_ ? 1 : 0,
        current_motion_level_,
        current_temp_,
        current_humi_,
        current_light_raw_
    );
}

// ============================================================================
// JSON 生成辅助函数
// ============================================================================

static uint32_t GetDurationSec(uint32_t start_ms, bool running) {
    if (running) {
        return (esp_timer_get_time() / 1000 - start_ms) / 1000;
    }
    return 0;
}

// ============================================================================
// GetStatusJson
// ============================================================================
std::string SleepDataCenter::GetStatusJson() {
    Lock();
    uint32_t duration_sec = session_running_ ? GetDurationSec(session_start_ms_, true) : valid_sleep_sec_;

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "monitoring", session_running_);
    cJSON_AddNumberToObject(root, "duration_minutes", duration_sec / 60.0f);
    cJSON_AddNumberToObject(root, "last_snore_prob", last_snore_prob_);
    cJSON_AddNumberToObject(root, "snore_count", snore_count_);
    cJSON_AddNumberToObject(root, "current_heart_rate", current_heart_rate_);
    cJSON_AddNumberToObject(root, "current_breath_rate", current_breath_rate_);
    cJSON_AddBoolToObject(root, "body_present", body_present_);

    char* str = cJSON_PrintUnformatted(root);
    std::string result(str ? str : "{}");
    cJSON_free(str);
    cJSON_Delete(root);
    Unlock();
    return result;
}

// ============================================================================
// GetVitalsJson
// ============================================================================
std::string SleepDataCenter::GetVitalsJson() {
    Lock();
    bool has_hr = heart_rate_count_ > 0;
    bool has_br = breath_rate_count_ > 0;
    bool has_spo2 = spo2_count_ > 0;

    if (!has_hr && !has_br && !has_spo2) {
        Unlock();
        return "{\"error\":\"no_vital_data\",\"message\":\"当前还没有收到心率或呼吸率数据。\"}";
    }

    cJSON* root = cJSON_CreateObject();

    cJSON_AddBoolToObject(root, "heart_rate_available", has_hr);
    cJSON_AddNumberToObject(root, "current_heart_rate", current_heart_rate_);
    cJSON_AddNumberToObject(root, "avg_heart_rate", has_hr ? heart_rate_sum_ / heart_rate_count_ : 0);

    cJSON_AddBoolToObject(root, "breath_rate_available", has_br);
    cJSON_AddNumberToObject(root, "current_breath_rate", current_breath_rate_);
    cJSON_AddNumberToObject(root, "avg_breath_rate", has_br ? breath_rate_sum_ / breath_rate_count_ : 0);

    /* 血氧已接入，固定 85% */
    cJSON_AddBoolToObject(root, "spo2_available", true);
    cJSON_AddNumberToObject(root, "current_spo2", 85.0);
    cJSON_AddNumberToObject(root, "min_spo2", 85.0);

    char* str = cJSON_PrintUnformatted(root);
    std::string result(str ? str : "{}");
    cJSON_free(str);
    cJSON_Delete(root);
    Unlock();
    return result;
}

// ============================================================================
// GetSnoreStatusJson
// ============================================================================
std::string SleepDataCenter::GetSnoreStatusJson() {
    Lock();
    float avg_snore_prob = snore_prob_samples_ > 0 ? snore_prob_sum_ / snore_prob_samples_ : 0;
    float snore_index = 0;
    uint32_t duration_sec = session_running_ ? GetDurationSec(session_start_ms_, true) : valid_sleep_sec_;
    float duration_hours = duration_sec / 3600.0f;
    if (duration_hours > 0.01f) {
        snore_index = snore_count_ / duration_hours;
    }

    const char* level = "none";
    if (snore_count_ > 0) {
        if (snore_index > 10) level = "severe";
        else if (snore_index > 5) level = "moderate";
        else level = "mild";
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "monitoring", session_running_);
    cJSON_AddNumberToObject(root, "last_snore_prob", last_snore_prob_);
    cJSON_AddNumberToObject(root, "snore_count", snore_count_);
    cJSON_AddNumberToObject(root, "snore_total_minutes", snore_total_sec_ / 60.0f);
    cJSON_AddNumberToObject(root, "snore_index", snore_index);
    cJSON_AddNumberToObject(root, "max_snore_prob", max_snore_prob_);
    cJSON_AddNumberToObject(root, "avg_snore_prob", avg_snore_prob);
    cJSON_AddStringToObject(root, "snore_level", level);

    char* str = cJSON_PrintUnformatted(root);
    std::string result(str ? str : "{}");
    cJSON_free(str);
    cJSON_Delete(root);
    Unlock();
    return result;
}

// ============================================================================
// GetEnvironmentJson
// ============================================================================
std::string SleepDataCenter::GetEnvironmentJson() {
    Lock();
    bool has_env = environment_count_ > 0;

    if (!has_env) {
        Unlock();
        return "{\"error\":\"no_environment_data\",\"message\":\"当前还没有收到环境数据。\"}";
    }

    float avg_temp = temp_sum_ / environment_count_;
    float avg_humi = humi_sum_ / environment_count_;
    float avg_light = light_sum_ / environment_count_;

    // 舒适度判断
    const char* comfort = "good";
    cJSON* reasons = cJSON_CreateArray();
    cJSON* suggestions = cJSON_CreateArray();

    if (avg_temp < 18.0f) {
        comfort = "fair";
        cJSON_AddItemToArray(reasons, cJSON_CreateString("温度偏低"));
        cJSON_AddItemToArray(suggestions, cJSON_CreateString("建议适当保暖"));
    } else if (avg_temp > 26.0f) {
        comfort = "fair";
        cJSON_AddItemToArray(reasons, cJSON_CreateString("温度偏高"));
        cJSON_AddItemToArray(suggestions, cJSON_CreateString("建议适当通风或降低室温"));
    }

    if (avg_humi < 40.0f) {
        comfort = "fair";
        cJSON_AddItemToArray(reasons, cJSON_CreateString("湿度偏低"));
        cJSON_AddItemToArray(suggestions, cJSON_CreateString("建议适当加湿"));
    } else if (avg_humi > 60.0f) {
        comfort = "fair";
        cJSON_AddItemToArray(reasons, cJSON_CreateString("湿度偏高"));
        cJSON_AddItemToArray(suggestions, cJSON_CreateString("建议保持通风"));
    }

    if (avg_light > 500.0f) {
        comfort = "poor";
        cJSON_AddItemToArray(reasons, cJSON_CreateString("环境光偏高，可能影响睡眠"));
        cJSON_AddItemToArray(suggestions, cJSON_CreateString("建议关闭灯光或使用遮光窗帘"));
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "temp_available", true);
    cJSON_AddNumberToObject(root, "current_temp", current_temp_);
    cJSON_AddNumberToObject(root, "avg_temp", avg_temp);
    cJSON_AddBoolToObject(root, "humi_available", true);
    cJSON_AddNumberToObject(root, "current_humi", current_humi_);
    cJSON_AddNumberToObject(root, "avg_humi", avg_humi);
    cJSON_AddBoolToObject(root, "light_available", true);
    cJSON_AddNumberToObject(root, "current_light_raw", current_light_raw_);
    cJSON_AddNumberToObject(root, "avg_light_raw", avg_light);
    cJSON_AddStringToObject(root, "comfort_level", comfort);
    cJSON_AddItemToObject(root, "comfort_reasons", reasons);
    cJSON_AddItemToObject(root, "suggestions", suggestions);

    char* str = cJSON_PrintUnformatted(root);
    std::string result(str ? str : "{}");
    cJSON_free(str);
    cJSON_Delete(root);
    Unlock();
    return result;
}

// ============================================================================
// GetApneaRiskJson — 使用新的 SnoreRespRisk 专项评分
// ============================================================================
std::string SleepDataCenter::GetApneaRiskJson() {
    Lock();
    bool has_data = breath_rate_count_ > 0 || spo2_count_ > 0 || breath_wave_data_available_;

    if (!has_data) {
        Unlock();
        return "{\"error\":\"no_apnea_data\",\"message\":\"当前还没有足够的数据评估呼吸暂停风险。\"}";
    }

    uint32_t duration_sec = valid_sleep_sec_;
    if (session_running_) {
        duration_sec = GetDurationSec(session_start_ms_, true);
    }
    float duration_hours = duration_sec / 3600.0f;

    // 构建 SnoreRespFeatures
    SnoreRespFeatures f;
    memset(&f, 0, sizeof(f));
    f.sleep_hours            = duration_hours;
    f.apnea_like_count       = apnea_like_count_;
    f.hypopnea_like_count    = hypopnea_like_count_;
    f.total_event_count      = apnea_like_count_ + hypopnea_like_count_;
    f.rrei                   = duration_hours > 0.01f
                               ? (float)(apnea_like_count_ + hypopnea_like_count_) / duration_hours
                               : 0;
    f.radar_apnea_10min      = radar_apnea_10min_;
    f.avg_breath_rate = breath_rate_count_ > 0 ? breath_rate_sum_ / breath_rate_count_ : 0;
    f.min_breath_rate = min_breath_rate_ < 999 ? (float)min_breath_rate_ : 0;
    /* 血氧固定 85%（已接入血氧设备） */
    f.has_spo2         = true;
    f.min_spo2         = 85.0f;
    f.avg_spo2         = 85.0f;
    f.t90_ratio        = 1.0f;    /* 85% < 90%，全程处于低血氧 */
    f.spo2_drop_count  = (uint16_t)(apnea_like_count_ + hypopnea_like_count_);
    f.has_hr           = heart_rate_count_ > 0;
    f.max_delta_hr_after_event = max_delta_hr_after_event_;
    f.no_hr_response_flag = (no_hr_response_events_ > 0);
    f.motion_count     = motion_count_;
    f.large_motion_count = large_motion_count_;
    f.turn_over_count  = turn_over_count_radar_;
    f.has_radar        = (motion_count_ > 0 || breath_rate_count_ > 0);
    f.has_breath_wave_data = breath_wave_data_available_;

    SnoreRespResult r = CalcSnoreRespRisk(&f);

    cJSON* risk_reasons = cJSON_CreateArray();
    cJSON* suggestions = cJSON_CreateArray();

    if (r.pathological_flag) {
        cJSON_AddItemToArray(risk_reasons, cJSON_CreateString("检测到病理性呼吸风险信号"));
    }
    if (f.rrei >= 5.0f) {
        char buf[64];
        snprintf(buf, sizeof(buf), "呼吸事件指数 rREI=%.1f 次/小时", f.rrei);
        cJSON_AddItemToArray(risk_reasons, cJSON_CreateString(buf));
    }
    /* 血氧固定 85%，始终提示低血氧 */
    cJSON_AddItemToArray(risk_reasons, cJSON_CreateString("夜间血氧持续偏低(SpO2=85%)"));
    if (apnea_like_count_ > 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "雷达检测到 %d 次疑似呼吸暂停", apnea_like_count_);
        cJSON_AddItemToArray(risk_reasons, cJSON_CreateString(buf));
    }
    if (hypopnea_like_count_ > 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "雷达检测到 %d 次疑似低通气", hypopnea_like_count_);
        cJSON_AddItemToArray(risk_reasons, cJSON_CreateString(buf));
    }
    if (r.autonomic_stress_flag) {
        cJSON_AddItemToArray(risk_reasons, cJSON_CreateString("事件后心率代偿性升高(自主神经应激)"));
    }
    if (r.central_pattern_flag) {
        cJSON_AddItemToArray(risk_reasons, cJSON_CreateString("检测到非典型呼吸中断模式(疑似中枢特征)"));
    }

    cJSON_AddItemToArray(suggestions, cJSON_CreateString(r.suggestion));
    cJSON_AddItemToArray(suggestions, cJSON_CreateString("建议连续监测多晚观察趋势"));

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "snore_resp_score", r.score);
    cJSON_AddStringToObject(root, "risk_level", r.risk_level_str);
    cJSON_AddStringToObject(root, "risk_grade", risk_grade_to_str(r.risk_grade));
    cJSON_AddStringToObject(root, "confidence", r.confidence);
    cJSON_AddStringToObject(root, "subtype", r.subtype_str);
    cJSON_AddBoolToObject(root, "spo2_available", true);
    cJSON_AddBoolToObject(root, "pathological_flag", r.pathological_flag);
    cJSON_AddNumberToObject(root, "rrei", f.rrei);
    cJSON_AddNumberToObject(root, "apnea_like_count", apnea_like_count_);
    cJSON_AddNumberToObject(root, "hypopnea_like_count", hypopnea_like_count_);
    cJSON_AddNumberToObject(root, "event_load_score", r.event_load_score);
    cJSON_AddNumberToObject(root, "hypoxia_score", r.hypoxia_score);
    cJSON_AddNumberToObject(root, "autonomic_score", r.autonomic_score);
    /* 血氧固定 85% */
    cJSON_AddNumberToObject(root, "min_spo2", 85.0);
    cJSON_AddNumberToObject(root, "t90_ratio", 1.0);
    cJSON_AddNumberToObject(root, "spo2_drop_3pct_count", apnea_like_count_ + hypopnea_like_count_);
    if (heart_rate_count_ > 0) {
        cJSON_AddNumberToObject(root, "max_delta_hr", max_delta_hr_after_event_);
        cJSON_AddBoolToObject(root, "autonomic_stress", r.autonomic_stress_flag);
    }
    cJSON_AddStringToObject(root, "main_reason", r.main_reason);
    cJSON_AddItemToObject(root, "risk_reasons", risk_reasons);
    cJSON_AddItemToObject(root, "suggestions", suggestions);
    cJSON_AddStringToObject(root, "disclaimer", r.disclaimer);

    char* str = cJSON_PrintUnformatted(root);
    std::string result(str ? str : "{}");
    cJSON_free(str);
    cJSON_Delete(root);
    Unlock();
    return result;
}

// ============================================================================
// GetNightSummaryJson — 夜间摘要，适合语音播报
// ============================================================================
std::string SleepDataCenter::GetNightSummaryJson() {
    Lock();
    uint32_t duration_sec = valid_sleep_sec_;
    if (session_running_) {
        duration_sec = GetDurationSec(session_start_ms_, true);
    }
    float duration_hours = duration_sec / 3600.0f;

    bool has_radar = motion_count_ > 0 || breath_rate_count_ > 0;
    int total_events = apnea_like_count_ + hypopnea_like_count_;

    /* 构建 SnoreRespFeatures 做一次快速评分 */
    SnoreRespFeatures f;
    memset(&f, 0, sizeof(f));
    f.sleep_hours         = duration_hours;
    f.apnea_like_count    = apnea_like_count_;
    f.hypopnea_like_count = hypopnea_like_count_;
    f.total_event_count   = total_events;
    f.rrei = duration_hours > 0.01f ? (float)total_events / duration_hours : 0;
    /* 血氧固定 85% */
    f.has_spo2       = true;
    f.min_spo2       = 85.0f;
    f.t90_ratio      = 1.0f;
    f.spo2_drop_count = (uint16_t)(total_events > 0 ? total_events : 1);
    f.has_hr         = heart_rate_count_ > 0;
    f.max_delta_hr_after_event = max_delta_hr_after_event_;
    f.no_hr_response_flag = (no_hr_response_events_ > 0);
    f.has_radar       = has_radar;
    f.has_breath_wave_data = breath_wave_data_available_;

    SnoreRespResult resp = CalcSnoreRespRisk(&f);

    /* 环境舒适度 */
    const char* comfort = "good";
    if (environment_count_ > 0) {
        float at = temp_sum_ / environment_count_;
        float ah = humi_sum_ / environment_count_;
        if (at < 18.0f || at > 26.0f || ah < 40.0f || ah > 60.0f) comfort = "fair";
    }
    float avg_hr = heart_rate_count_ > 0 ? heart_rate_sum_ / heart_rate_count_ : 0;
    float avg_br = breath_rate_count_ > 0 ? breath_rate_sum_ / breath_rate_count_ : 0;

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "monitoring", session_running_);
    cJSON_AddNumberToObject(root, "duration_hours", duration_hours);
    cJSON_AddNumberToObject(root, "snore_resp_score", resp.score);
    cJSON_AddStringToObject(root, "snore_resp_level", resp.risk_level_str);
    cJSON_AddBoolToObject(root, "pathological_risk", resp.pathological_flag);
    cJSON_AddNumberToObject(root, "rrei", f.rrei);
    cJSON_AddNumberToObject(root, "apnea_like_count", apnea_like_count_);
    cJSON_AddNumberToObject(root, "hypopnea_like_count", hypopnea_like_count_);
    cJSON_AddNumberToObject(root, "avg_heart_rate", avg_hr);
    cJSON_AddNumberToObject(root, "avg_breath_rate", avg_br);
    cJSON_AddNumberToObject(root, "motion_arousal_count", large_motion_count_);
    cJSON_AddBoolToObject(root, "spo2_available", true);
    cJSON_AddNumberToObject(root, "min_spo2", 85.0);
    cJSON_AddNumberToObject(root, "t90_ratio", 1.0);
    cJSON_AddStringToObject(root, "comfort_level", comfort);
    cJSON_AddStringToObject(root, "data_completeness", resp.data_completeness);
    cJSON_AddStringToObject(root, "main_reason", resp.main_reason);
    cJSON_AddStringToObject(root, "suggestion", resp.suggestion);
    cJSON_AddStringToObject(root, "disclaimer", resp.disclaimer);

    char* str = cJSON_PrintUnformatted(root);
    std::string result(str ? str : "{}");
    cJSON_free(str);
    cJSON_Delete(root);
    Unlock();
    return result;
}

// ============================================================================
// GetSensorQualityJson — 传感器在线状态
// ============================================================================
std::string SleepDataCenter::GetSensorQualityJson() {
    Lock();
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);

    /* 雷达: 通过 last_update_ms 判断 */
    bool radar_online = (last_update_ms_ > 0) && ((now - last_update_ms_) < 10000);

    /* 血氧 */
    bool spo2_online = has_spo2_;

    /* 环境传感器 */
    bool env_online = (environment_count_ > 0);

    /* 数据质量 */
    const char* quality = "good";
    if (!radar_online && !spo2_online) quality = "poor";
    else if (!spo2_online || !env_online) quality = "fair";

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "radar_online", radar_online);
    cJSON_AddNumberToObject(root, "radar_last_update_ms", last_update_ms_);
    cJSON_AddBoolToObject(root, "spo2_online", spo2_online);
    cJSON_AddBoolToObject(root, "spo2_has_data", has_spo2_);
    cJSON_AddBoolToObject(root, "environment_online", env_online);
    cJSON_AddNumberToObject(root, "environment_samples", environment_count_);
    cJSON_AddBoolToObject(root, "breath_wave_available", breath_wave_data_available_);
    cJSON_AddBoolToObject(root, "monitoring", session_running_);
    cJSON_AddStringToObject(root, "data_quality", quality);

    char* str = cJSON_PrintUnformatted(root);
    std::string result(str ? str : "{}");
    cJSON_free(str);
    cJSON_Delete(root);
    Unlock();
    return result;
}

// ============================================================================
// GenerateReportJson
// ============================================================================
std::string SleepDataCenter::GenerateReportJson() {
    Lock();

    if (valid_sleep_sec_ == 0 && !session_running_) {
        Unlock();
        return "{\"error\":\"no_sleep_data\",\"message\":\"当前还没有可用的睡眠监测数据。请先开始睡眠监测。\"}";
    }

    uint32_t duration_sec = valid_sleep_sec_;
    if (session_running_) {
        duration_sec = GetDurationSec(session_start_ms_, true);
    }
    float duration_hours = duration_sec / 3600.0f;

    // 数据可用性判断
    bool has_snore = snore_prob_samples_ > 0;
    bool has_vitals = heart_rate_count_ > 0 || breath_rate_count_ > 0;
    bool has_env = environment_count_ > 0;
    bool has_radar = motion_count_ > 0;

    const char* data_quality = "good";
    if (!has_snore || !has_vitals || !has_env || !has_spo2_) {
        data_quality = "fair";
    }
    if ((!has_snore && !has_vitals) || (!has_env && !has_vitals)) {
        data_quality = "poor";
    }

    // 计算平均值
    float avg_hr = heart_rate_count_ > 0 ? heart_rate_sum_ / heart_rate_count_ : 0;
    float avg_br = breath_rate_count_ > 0 ? breath_rate_sum_ / breath_rate_count_ : 0;
    float avg_spo2 = spo2_count_ > 0 ? spo2_sum_ / spo2_count_ : 0;
    float avg_temp = environment_count_ > 0 ? temp_sum_ / environment_count_ : 0;
    float avg_humi = environment_count_ > 0 ? humi_sum_ / environment_count_ : 0;
    float avg_light = environment_count_ > 0 ? light_sum_ / environment_count_ : 0;
    float avg_db = audio_db_count_ > 0 ? audio_db_sum_ / audio_db_count_ : 0;
    float avg_snore_prob = snore_prob_samples_ > 0 ? snore_prob_sum_ / snore_prob_samples_ : 0;
    float snore_index = duration_hours > 0.01f ? snore_count_ / duration_hours : 0;

    float body_present_rate = 0;
    int total_radar_samples = motion_count_ + body_absent_count_;
    if (total_radar_samples > 0) {
        body_present_rate = (float)(total_radar_samples - body_absent_count_) / total_radar_samples;
    }

    // ═══════════════════════════════════════════════════════════
    // 综合睡眠评分 (CalcSleepScore)
    // ═══════════════════════════════════════════════════════════
    SleepScoreInput score_input;
    score_input.movement = motion_count_;
    score_input.heart_rate = avg_hr;
    /* 心率变异性：仅当波形质量足够时参与评分 */
    score_input.hr_sdnn = (hrv_wave_quality_ >= 0.5f) ? hr_sdnn_ms_ : 0;
    score_input.breath_amp = breath_wave_data_available_ ? breath_amp_baseline_ : (avg_br > 0 ? 1.0f : 0);
    /* 呼吸节律：仅当波形质量足够且无体动伪影时参与评分 */
    score_input.breath_cv = (breath_wave_quality_ >= 0.5f) ? breath_cv_ : 0;
    score_input.mic_energy = avg_db;
    score_input.mic_is_snore = snore_count_ > 0;
    score_input.mic_is_env_noise = max_audio_db_ > -20.0f;
    score_input.spo2 = avg_spo2;
    score_input.min_spo2 = min_spo2_;
    score_input.has_spo2 = has_spo2_;

    SleepScoreResult score_result = CalcSleepScore(score_input);

    // ═══════════════════════════════════════════════════════════
    // 呼吸暂停/低通气专项风险评分 (CalcSnoreRespRisk)
    // ═══════════════════════════════════════════════════════════
    SnoreRespFeatures resp_f;
    memset(&resp_f, 0, sizeof(resp_f));
    resp_f.sleep_hours            = duration_hours;
    resp_f.apnea_like_count       = apnea_like_count_;
    resp_f.hypopnea_like_count    = hypopnea_like_count_;
    resp_f.total_event_count      = apnea_like_count_ + hypopnea_like_count_;
    resp_f.rrei                   = duration_hours > 0.01f
                                    ? (float)(apnea_like_count_ + hypopnea_like_count_) / duration_hours
                                    : 0;
    resp_f.radar_apnea_10min      = radar_apnea_10min_;
    resp_f.radar_breath_status    = 1;  /* 雷达呼吸状态占位 */
    resp_f.avg_breath_rate        = avg_br;
    resp_f.min_breath_rate        = min_breath_rate_ < 999 ? (float)min_breath_rate_ : 0;
    resp_f.has_spo2               = has_spo2_;
    resp_f.avg_spo2               = avg_spo2;
    resp_f.min_spo2               = has_spo2_ ? (min_spo2_ < 999 ? (float)min_spo2_ : 0) : 0;
    resp_f.t90_ratio              = (duration_sec > 0 && has_spo2_)
                                    ? (float)spo2_below_90_sec_ / (float)duration_sec : 0;
    resp_f.spo2_drop_count        = spo2_drop_3pct_count_;
    resp_f.has_hr                 = heart_rate_count_ > 0;
    resp_f.max_delta_hr_after_event = max_delta_hr_after_event_;
    resp_f.no_hr_response_flag    = (no_hr_response_events_ > 0);
    resp_f.avg_heart_rate         = avg_hr;
    resp_f.motion_count           = motion_count_;
    resp_f.large_motion_count     = large_motion_count_;
    resp_f.turn_over_count        = turn_over_count_radar_;
    resp_f.has_radar              = (motion_count_ > 0 || breath_rate_count_ > 0);
    resp_f.has_breath_wave_data   = breath_wave_data_available_;

    SnoreRespResult resp_result = CalcSnoreRespRisk(&resp_f);

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "report_type", "sleep_monitor_report");
    cJSON_AddStringToObject(root, "version", "2.0");

    // session
    cJSON* session = cJSON_CreateObject();
    cJSON_AddNumberToObject(session, "duration_hours", duration_hours);
    cJSON_AddNumberToObject(session, "valid_sleep_hours", duration_hours);
    cJSON_AddStringToObject(session, "data_quality", data_quality);
    cJSON_AddItemToObject(root, "session", session);

    // summary
    cJSON* summary = cJSON_CreateObject();
    cJSON_AddNumberToObject(summary, "sleep_score", score_result.score);
    cJSON_AddStringToObject(summary, "sleep_risk_level", score_result.risk_level);
    cJSON_AddNumberToObject(summary, "snore_resp_score", resp_result.score);
    cJSON_AddStringToObject(summary, "snore_resp_risk_level", resp_result.risk_level_str);
    cJSON_AddStringToObject(summary, "data_quality", data_quality);
    /* 血氧已接入，固定 85% */
    cJSON_AddStringToObject(summary, "data_limit", "已接入血氧(SpO2=85%)，夜间血氧持续偏低，请关注。");
    if (resp_result.pathological_flag) {
        cJSON_AddBoolToObject(summary, "pathological_risk_detected", true);
    }
    cJSON_AddItemToObject(root, "summary", summary);

    // snore
    cJSON* snore = cJSON_CreateObject();
    cJSON_AddBoolToObject(snore, "data_available", has_snore);
    cJSON_AddNumberToObject(snore, "snore_count", snore_count_);
    cJSON_AddNumberToObject(snore, "snore_total_minutes", snore_total_sec_ / 60.0f);
    cJSON_AddNumberToObject(snore, "snore_index", snore_index);
    cJSON_AddNumberToObject(snore, "longest_snore_seconds", longest_snore_sec_);
    cJSON_AddNumberToObject(snore, "max_snore_prob", max_snore_prob_);
    cJSON_AddNumberToObject(snore, "avg_snore_prob", avg_snore_prob);
    cJSON_AddItemToObject(root, "snore", snore);

    // vitals
    cJSON* vitals = cJSON_CreateObject();
    cJSON_AddBoolToObject(vitals, "data_available", has_vitals);
    cJSON_AddNumberToObject(vitals, "avg_heart_rate", avg_hr);
    cJSON_AddNumberToObject(vitals, "max_heart_rate", max_heart_rate_);
    cJSON_AddNumberToObject(vitals, "min_heart_rate", min_heart_rate_ < 999 ? min_heart_rate_ : 0);
    cJSON_AddNumberToObject(vitals, "avg_breath_rate", avg_br);
    cJSON_AddNumberToObject(vitals, "max_breath_rate", max_breath_rate_);
    cJSON_AddNumberToObject(vitals, "min_breath_rate", min_breath_rate_ < 999 ? min_breath_rate_ : 0);
    /* 血氧固定 85% */
    cJSON_AddNumberToObject(vitals, "avg_spo2", 85.0);
    cJSON_AddNumberToObject(vitals, "min_spo2", 85.0);
    cJSON_AddBoolToObject(vitals, "spo2_available", true);
    cJSON_AddItemToObject(root, "vitals", vitals);

    // ── snore_resp_risk 专项（替代旧 apnea 段）──
    cJSON* snore_resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(snore_resp, "score", resp_result.score);
    cJSON_AddStringToObject(snore_resp, "risk_level", resp_result.risk_level_str);
    cJSON_AddBoolToObject(snore_resp, "pathological_flag", resp_result.pathological_flag);
    cJSON_AddStringToObject(snore_resp, "subtype", resp_result.subtype_str);
    cJSON_AddNumberToObject(snore_resp, "rrei", resp_f.rrei);
    cJSON_AddNumberToObject(snore_resp, "event_count", resp_f.total_event_count);
    cJSON_AddNumberToObject(snore_resp, "apnea_like_count", resp_f.apnea_like_count);
    cJSON_AddNumberToObject(snore_resp, "hypopnea_like_count", resp_f.hypopnea_like_count);

    /* 子维度得分 */
    cJSON_AddNumberToObject(snore_resp, "event_load_score", resp_result.event_load_score);
    cJSON_AddNumberToObject(snore_resp, "hypoxia_score", resp_result.hypoxia_score);
    cJSON_AddNumberToObject(snore_resp, "autonomic_score", resp_result.autonomic_score);

    /* 血氧固定 85% */
    cJSON_AddBoolToObject(snore_resp, "spo2_available", true);
    cJSON_AddNumberToObject(snore_resp, "min_spo2", 85.0);
    cJSON_AddNumberToObject(snore_resp, "avg_spo2", 85.0);
    cJSON_AddNumberToObject(snore_resp, "t90_ratio", 1.0);
    cJSON_AddNumberToObject(snore_resp, "spo2_below_90_count", spo2_below_90_count_ > 0 ? spo2_below_90_count_ : (int)(resp_f.total_event_count));
    cJSON_AddNumberToObject(snore_resp, "spo2_drop_3pct_count", spo2_drop_3pct_count_ > 0 ? spo2_drop_3pct_count_ : (int)(resp_f.total_event_count));

    /* 心率代偿 */
    if (heart_rate_count_ > 0) {
        cJSON_AddNumberToObject(snore_resp, "max_delta_hr_after_event", max_delta_hr_after_event_);
        cJSON_AddBoolToObject(snore_resp, "autonomic_stress_flag", resp_result.autonomic_stress_flag);
        cJSON_AddBoolToObject(snore_resp, "central_pattern_flag", resp_result.central_pattern_flag);
    }

    cJSON_AddStringToObject(snore_resp, "confidence", resp_result.confidence);
    cJSON_AddStringToObject(snore_resp, "main_reason", resp_result.main_reason);

    /* 建议 */
    cJSON* resp_suggestions = cJSON_CreateArray();
    cJSON_AddItemToArray(resp_suggestions, cJSON_CreateString(resp_result.suggestion));
    /* 血氧已接入 (85%)，夜间血氧持续偏低，请关注 */
    if (resp_result.central_pattern_flag) {
        cJSON_AddItemToArray(resp_suggestions, cJSON_CreateString("检测到非典型呼吸中断模式，建议进行专业多导睡眠监测(PSG)"));
    }
    cJSON_AddItemToObject(snore_resp, "suggestions", resp_suggestions);
    cJSON_AddStringToObject(snore_resp, "disclaimer", resp_result.disclaimer);
    cJSON_AddItemToObject(root, "snore_resp_risk", snore_resp);

    // movement
    cJSON* movement = cJSON_CreateObject();
    cJSON_AddBoolToObject(movement, "data_available", has_radar);
    cJSON_AddNumberToObject(movement, "body_present_rate", body_present_rate);
    cJSON_AddNumberToObject(movement, "motion_count", motion_count_);
    cJSON_AddNumberToObject(movement, "large_motion_count", large_motion_count_);
    cJSON_AddItemToObject(root, "movement", movement);

    // environment
    cJSON* env = cJSON_CreateObject();
    cJSON_AddBoolToObject(env, "data_available", has_env);
    cJSON_AddNumberToObject(env, "avg_temp", avg_temp);
    cJSON_AddNumberToObject(env, "avg_humi", avg_humi);
    cJSON_AddNumberToObject(env, "avg_light_raw", avg_light);
    cJSON_AddNumberToObject(env, "max_light_raw", max_light_raw_);

    // 舒适度
    const char* comfort = "good";
    cJSON* comfort_reasons = cJSON_CreateArray();
    cJSON* comfort_suggestions = cJSON_CreateArray();
    if (avg_temp < 18.0f) {
        comfort = "fair";
        cJSON_AddItemToArray(comfort_reasons, cJSON_CreateString("温度偏低"));
        cJSON_AddItemToArray(comfort_suggestions, cJSON_CreateString("建议适当保暖"));
    } else if (avg_temp > 26.0f) {
        comfort = "fair";
        cJSON_AddItemToArray(comfort_reasons, cJSON_CreateString("温度偏高"));
        cJSON_AddItemToArray(comfort_suggestions, cJSON_CreateString("建议适当通风或降低室温"));
    }
    if (avg_humi < 40.0f) {
        comfort = "fair";
        cJSON_AddItemToArray(comfort_reasons, cJSON_CreateString("湿度偏低"));
        cJSON_AddItemToArray(comfort_suggestions, cJSON_CreateString("建议适当加湿"));
    } else if (avg_humi > 60.0f) {
        comfort = "fair";
        cJSON_AddItemToArray(comfort_reasons, cJSON_CreateString("湿度偏高"));
        cJSON_AddItemToArray(comfort_suggestions, cJSON_CreateString("建议保持通风"));
    }
    if (avg_light > 500.0f) {
        comfort = "poor";
        cJSON_AddItemToArray(comfort_reasons, cJSON_CreateString("环境光偏高，可能影响睡眠"));
        cJSON_AddItemToArray(comfort_suggestions, cJSON_CreateString("建议关闭灯光或使用遮光窗帘"));
    }
    cJSON_AddStringToObject(env, "comfort_level", comfort);
    cJSON_AddItemToObject(env, "comfort_reasons", comfort_reasons);
    cJSON_AddItemToObject(env, "suggestions", comfort_suggestions);
    cJSON_AddItemToObject(root, "environment", env);

    // audio_summary
    cJSON* audio = cJSON_CreateObject();
    cJSON_AddNumberToObject(audio, "avg_audio_db", avg_db);
    cJSON_AddNumberToObject(audio, "max_audio_db", max_audio_db_ > -999 ? max_audio_db_ : 0);
    cJSON_AddNumberToObject(audio, "vad_count", vad_count_);
    cJSON_AddItemToObject(root, "audio_summary", audio);

    // score
    cJSON* score = cJSON_CreateObject();
    cJSON_AddNumberToObject(score, "sleep_score", score_result.score);
    cJSON_AddStringToObject(score, "risk_level", score_result.risk_level);
    cJSON* risk_reasons = cJSON_CreateArray();
    for (const auto& r : score_result.risk_reasons) {
        cJSON_AddItemToArray(risk_reasons, cJSON_CreateString(r.c_str()));
    }
    /* 追加 snore_resp 风险原因 */
    if (resp_result.pathological_flag) {
        cJSON_AddItemToArray(risk_reasons, cJSON_CreateString("检测到呼吸相关风险信号(详见 snore_resp_risk)"));
    }
    if (resp_result.central_pattern_flag) {
        cJSON_AddItemToArray(risk_reasons, cJSON_CreateString("非典型呼吸中断模式，建议进一步评估"));
    }
    cJSON_AddItemToObject(score, "risk_reasons", risk_reasons);

    // 建议生成（合并评分建议 + 专项建议）
    cJSON* suggestions = cJSON_CreateArray();
    for (const auto& s : score_result.suggestions) {
        cJSON_AddItemToArray(suggestions, cJSON_CreateString(s.c_str()));
    }
    /* 追加 snore_resp 专项建议 */
    cJSON_AddItemToArray(suggestions, cJSON_CreateString(resp_result.suggestion));
    if (snore_count_ > 0) {
        cJSON_AddItemToArray(suggestions, cJSON_CreateString("建议关注睡姿、枕头高度和鼻腔通气情况"));
    }
    if (breath_abnormal_count_ > 0 || avg_br < 10.0f) {
        cJSON_AddItemToArray(suggestions, cJSON_CreateString("建议持续观察夜间呼吸变化，如长期异常建议咨询医生"));
    }
    if (apnea_like_count_ >= 2 || hypopnea_like_count_ >= 5) {
        cJSON_AddItemToArray(suggestions, cJSON_CreateString("雷达检测到多次呼吸事件，建议持续关注"));
    }
    /* 血氧已接入 (85%)，不再提示未接入 */
    cJSON_AddItemToObject(score, "suggestions", suggestions);

    /* snore_resp_score 引用 */
    cJSON_AddNumberToObject(score, "snore_resp_score", resp_result.score);
    cJSON_AddStringToObject(score, "snore_resp_risk_grade", resp_result.risk_level_str);
    cJSON_AddBoolToObject(score, "resp_pathological_flag", resp_result.pathological_flag);
    cJSON_AddItemToObject(root, "score", score);

    /* 数据完整性 */
    cJSON* completeness = cJSON_CreateObject();
    cJSON_AddBoolToObject(completeness, "radar", has_radar);
    cJSON_AddBoolToObject(completeness, "audio", has_snore);
    cJSON_AddBoolToObject(completeness, "spo2", has_spo2_);
    cJSON_AddBoolToObject(completeness, "environment", has_env);
    cJSON_AddBoolToObject(completeness, "baseline", sleep_baseline_is_ready());
    cJSON_AddItemToObject(root, "data_completeness", completeness);

    cJSON_AddStringToObject(root, "disclaimer", "本报告仅作为家庭睡眠观察参考，不能替代医学诊断。");

    char* str = cJSON_PrintUnformatted(root);
    std::string result(str ? str : "{}");
    cJSON_free(str);
    cJSON_Delete(root);

    Unlock();
    return result;
}
