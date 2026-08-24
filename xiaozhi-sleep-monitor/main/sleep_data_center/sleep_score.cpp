#include "sleep_score.h"

SleepScoreResult CalcSleepScore(const SleepScoreInput& input) {
    SleepScoreResult result;
    result.score = 100;

    // 1. SpO2 扣分（必须有血氧数据才参与评分）
    if (input.has_spo2 && input.spo2 > 0) {
        if (input.min_spo2 < 90.0f) {
            result.score -= 15;
            result.risk_reasons.push_back("血氧饱和度偏低");
        } else if (input.spo2 < 95.0f) {
            result.score -= 5;
            result.risk_reasons.push_back("血氧轻微不足");
        }
    }

    // 2. 心率扣分
    if (input.heart_rate > 0) {
        if (input.heart_rate > 100.0f || input.heart_rate < 50.0f) {
            result.score -= 10;
            result.risk_reasons.push_back("夜间心率异常");
        }
    }

    // 3. 心率变异性
    if (input.hr_sdnn > 0 && input.hr_sdnn < 20.0f) {
        result.score -= 5;
        result.risk_reasons.push_back("心率变异性偏低");
    }

    // 4. 呼吸幅度
    if (input.breath_amp > 0 && input.breath_amp < 0.3f) {
        result.score -= 10;
        result.risk_reasons.push_back("呼吸幅度偏浅");
    }

    // 5. 呼吸不规律
    if (input.breath_cv > 0 && input.breath_cv > 0.3f) {
        result.score -= 10;
        result.risk_reasons.push_back("呼吸节律不稳定");
    }

    // 6. 体动
    if (input.movement > 20.0f) {
        result.score -= 10;
        result.risk_reasons.push_back("夜间存在较多体动");
    } else if (input.movement > 10.0f) {
        result.score -= 5;
        result.risk_reasons.push_back("夜间存在少量体动");
    }

    // 7. 鼾声
    if (input.mic_is_snore) {
        result.score -= 10;
        result.risk_reasons.push_back("监测到疑似鼾声");
    }

    // 8. 环境噪声
    if (input.mic_is_env_noise || input.mic_energy > -20.0f) {
        result.score -= 5;
        result.risk_reasons.push_back("睡眠环境较嘈杂");
    }

    // 限制分数范围
    if (result.score < 0) result.score = 0;
    if (result.score > 100) result.score = 100;

    // 风险等级
    if (result.score >= 80) {
        result.risk_level = "low";
    } else if (result.score >= 60) {
        result.risk_level = "medium";
    } else {
        result.risk_level = "high";
    }

    // 默认建议
    if (result.risk_reasons.empty()) {
        result.suggestions.push_back("睡眠质量良好，请保持规律作息");
    } else {
        if (input.mic_is_snore) {
            result.suggestions.push_back("建议关注睡姿和枕头高度");
        }
        if (input.movement > 10.0f) {
            result.suggestions.push_back("建议睡前放松，减少夜间翻身");
        }
        if (input.has_spo2 && input.spo2 > 0 && input.spo2 < 95.0f) {
            result.suggestions.push_back("建议保持卧室通风");
        }
        if (input.mic_is_env_noise || input.mic_energy > -20.0f) {
            result.suggestions.push_back("建议降低睡眠环境噪音");
        }
        result.suggestions.push_back("建议连续监测多晚观察趋势");
    }

    return result;
}
