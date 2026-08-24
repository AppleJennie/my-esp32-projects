#pragma once

#include <vector>
#include <string>

struct SleepScoreInput {
    float movement = 0;          // 体动次数/强度
    float heart_rate = 0;        // 平均心率
    float hr_sdnn = 0;           // 心率变异性 SDNN
    float breath_amp = 0;        // 呼吸幅度
    float breath_cv = 0;         // 呼吸变异系数
    float mic_energy = 0;        // 麦克风能量 (dB)
    bool mic_is_snore = false;   // 是否有鼾声
    bool mic_is_env_noise = false; // 是否有环境噪声
    float spo2 = 0;              // 血氧饱和度（仅 has_spo2=true 时有效）
    float min_spo2 = 0;          // 最低血氧（仅 has_spo2=true 时有效）
    bool has_spo2 = false;       // 是否有血氧数据
};

struct SleepScoreResult {
    int score = 0;                          // 0-100
    const char* risk_level = "low";         // low / medium / high
    std::vector<std::string> risk_reasons;
    std::vector<std::string> suggestions;
};

SleepScoreResult CalcSleepScore(const SleepScoreInput& input);
