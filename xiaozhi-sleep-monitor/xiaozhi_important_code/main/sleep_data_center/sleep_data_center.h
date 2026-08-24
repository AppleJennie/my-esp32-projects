#pragma once

#include <string>
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class SleepDataCenter {
public:
    static SleepDataCenter& GetInstance();

    void Init();
    void StartSession();
    void StopSession();

    void UpdateSnore(float prob, bool is_snore, uint32_t window_sec);
    void UpdateRadar(int exist, int motion, int hr, int br, int dist);
    void UpdateEnvironment(float temp, float humi, int light_raw);
    void UpdateAudioSummary(float audio_db, int vad);
    void UpdateSpo2(float spo2);

    /**
     * @brief 更新呼吸波形幅度（由 radar adapter 每帧调用）
     *        用于实时检测呼吸暂停/低通气事件
     */
    void UpdateBreathWave(float breath_amp, uint32_t now_ms);

    /**
     * @brief 更新雷达 composite 数据（0x84 0x0C）
     */
    void UpdateRadarComposite(uint8_t apnea_10min, uint8_t turn_over_10min);

    /**
     * @brief 更新雷达波形深度分析结果（每 tick 调用）
     */
    void UpdateWaveFeatures(float breath_cv, float breath_pause_sec,
                            float breath_quality, float hr_sdnn_ms, float hrv_quality);

    /**
     * @brief 统一呼吸事件记录（所有事件源调用此方法）
     * @param type 1=apnea, 2=hypopnea
     * @param duration_sec 持续秒数
     * @param confidence 置信度 0~100
     * @param source 0=radar_only, 1=fusion(radar+audio)
     */
    void RecordBreathEvent(int type, uint16_t duration_sec, uint8_t confidence, int source);

    /**
     * @brief 写入一次 AI 识别到的鼾声事件（带 3000ms 合并去重）
     */
    void AddSnoreEvent(uint32_t timestamp_ms, uint32_t duration_ms,
                       float snore_prob, float rms_energy);

    std::string GenerateReportJson();
    std::string GetStatusJson();
    std::string GetVitalsJson();
    std::string GetSnoreStatusJson();
    std::string GetEnvironmentJson();
    std::string GetApneaRiskJson();
    std::string GetNightSummaryJson();
    std::string GetSensorQualityJson();
    int  GetCachedSnoreRespScore() const { return cached_snore_resp_score_; }
    void UpdateCachedSnoreRespScore();
    bool IsRunning() const;

private:
    SleepDataCenter() = default;
    SleepDataCenter(const SleepDataCenter&) = delete;
    SleepDataCenter& operator=(const SleepDataCenter&) = delete;

    void ResetStats();
    void Lock() const;
    void Unlock() const;
    void FlushRealtimeCsv();

    mutable SemaphoreHandle_t mutex_ = nullptr;

    bool session_running_ = false;
    uint32_t session_start_ms_ = 0;
    uint32_t session_stop_ms_ = 0;
    uint32_t valid_sleep_sec_ = 0;

    // ====== 实时值 ======
    int current_heart_rate_ = 0;
    int current_breath_rate_ = 0;
    float current_spo2_ = 0;
    float current_temp_ = 0;
    float current_humi_ = 0;
    int current_light_raw_ = 0;
    float current_audio_db_ = 0;
    int current_motion_level_ = 0;
    bool body_present_ = false;
    float last_snore_prob_ = 0;
    uint32_t last_update_ms_ = 0;

    // ====== 鼾声累计 ======
    int snore_count_ = 0;
    float snore_total_sec_ = 0;
    float longest_snore_sec_ = 0;
    float max_snore_prob_ = 0;
    float snore_prob_sum_ = 0;
    int snore_prob_samples_ = 0;
    float snore_current_duration_ = 0;

    // ====== 鼾声事件合并（AddSnoreEvent） ======
    uint32_t last_snore_event_ts_ms_ = 0;
    float    last_snore_event_max_prob_ = 0;
    float    last_snore_event_energy_sum_ = 0;
    int      last_snore_event_window_count_ = 0;

    // ====== 心率累计 ======
    float heart_rate_sum_ = 0;
    int heart_rate_count_ = 0;
    int max_heart_rate_ = 0;
    int min_heart_rate_ = 999;

    // ====== 呼吸累计 ======
    float breath_rate_sum_ = 0;
    int breath_rate_count_ = 0;
    int max_breath_rate_ = 0;
    int min_breath_rate_ = 999;
    int breath_abnormal_count_ = 0;

    // ====== 血氧累计 ======
    bool has_spo2_ = false;
    float spo2_sum_ = 0;
    int spo2_count_ = 0;
    float min_spo2_ = 999;
    int spo2_below_90_count_ = 0;
    int spo2_below_90_sec_ = 0;

    // ====== 体动累计 ======
    int motion_count_ = 0;
    int large_motion_count_ = 0;
    int body_absent_count_ = 0;

    // ====== 环境累计 ======
    float temp_sum_ = 0;
    float humi_sum_ = 0;
    float light_sum_ = 0;
    int environment_count_ = 0;
    int max_light_raw_ = 0;

    // ====== 音频累计 ======
    float audio_db_sum_ = 0;
    int audio_db_count_ = 0;
    float max_audio_db_ = -999;
    int vad_count_ = 0;

    // ====== 呼吸暂停风险累计 ======
    int apnea_suspected_count_ = 0;
    const char* apnea_risk_level_ = "low";
    const char* apnea_confidence_ = "medium";

    int cached_snore_resp_score_ = 0;       /* 每 tick 更新，给 MAIN 报告用 */

    // ====== 呼吸波形事件检测（纯雷达，无音频） ======
    bool    breath_wave_data_available_ = false;
    int     apnea_like_count_ = 0;          /* 波形幅度下降 ≥90% 持续 ≥10s */
    int     hypopnea_like_count_ = 0;       /* 波形幅度下降 30~90% 持续 ≥10s */
    int     resp_event_total_sec_ = 0;      /* 呼吸事件总持续秒数（用于 T90 近似） */

    uint8_t radar_apnea_10min_ = 0;         /* 雷达 composite 0x84 0x0C */
    uint8_t turn_over_count_radar_ = 0;     /* 雷达 composite 翻身次数 */

    float   max_delta_hr_after_event_ = 0;  /* 事件后心率最大上升幅度 */
    int     no_hr_response_events_ = 0;     /* 呼吸中断但心率无明显变化的次数 */

    uint16_t spo2_drop_3pct_count_ = 0;     /* SpO2 下降 ≥3% 次数 */
    float    last_spo2_baseline_ = 0;       /* 用于检测 SpO2 下降 */

    /* ── 波形深度特征（由 radar_wave_analyzer 计算） ── */
    float    breath_cv_ = 0;                /* 呼吸周期变异系数 */
    float    breath_pause_sec_ = 0;         /* 连续低幅时长 */
    float    breath_wave_quality_ = 0;      /* 呼吸波形质量 0~1 */
    float    hr_sdnn_ms_ = 0;              /* 心率变异性代理 SDNN (ms) */
    float    hrv_wave_quality_ = 0;         /* 心率波形质量 0~1 */

    /* 滚动窗口 — breath_cv / hr_sdnn 计算 */
    float    hr_history_[16];
    int      hr_history_idx_ = 0;
    int      hr_history_count_ = 0;

    /* 呼吸事件状态机（内部） */
    float    breath_amp_baseline_ = 0;      /* 30s 滚动基线 */
    uint32_t last_event_state_ms_ = 0;      /* 状态机时间戳 */
    uint16_t current_event_duration_sec_ = 0;
    int      current_event_type_ = 0;       /* 0=none, 1=apnea, 2=hypopnea */
    float    hr_before_event_ = 0;
    float    breath_amp_min_during_event_ = 0;
};
