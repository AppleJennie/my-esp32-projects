/**
 * @file    sleep_audio_adapter.c
 * @brief   音频适配层：桥接 audio_pipeline 和 SleepData_t
 *
 * 数据流:
 *   INMP441 → ring buffer → audio_input_read_pcm()
 *   → audio_pipeline_task (TFLite Micro 推理)
 *   → audio_feature_t → SleepData_t → sleep_ui_refresh()
 *
 * 注意: 不调用 LVGL API, 不创建第二个 LVGL 任务
 */

#include "sleep_audio_adapter.h"
#include "sleep_project_config.h"
#include "sleep_data.h"
#include "inmp441_i2s.h"
#include "audio_pipeline.h"
#include "fusion_types.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "AUDIO_ADAPTER";

static bool s_initialized     = false;
static bool s_last_snoring    = false;
static int  s_max_snore_db    = 0;

/* ═══════════════════════════════════════════════════════════════
 * 初始化
 * ═══════════════════════════════════════════════════════════════ */

esp_err_t sleep_audio_adapter_init(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "=== Sleep Audio Adapter Init Start ===");

    ret = inmp441_i2s_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "inmp441_i2s_init FAIL: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "[1/4] INMP441 I2S init OK");

    ret = inmp441_audio_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "inmp441_audio_start FAIL: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "[2/4] INMP441 capture task started");

    if (!audio_pipeline_init()) {
        ESP_LOGE(TAG, "audio_pipeline_init FAIL");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "[3/4] Audio pipeline init OK");

    BaseType_t task_ret = xTaskCreatePinnedToCore(
        audio_pipeline_task, "audio_pipe", 10240, NULL, 5, NULL, 1);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "audio_pipeline_task create FAIL");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "[4/4] Audio pipeline task (CPU1, prio=5)");

    s_initialized = true;
    ESP_LOGI(TAG, "=== Init Complete ===");
    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════════════
 * 更新 (LVGL 主循环每 500ms 调用)
 * ═══════════════════════════════════════════════════════════════ */

void sleep_audio_adapter_update(void)
{
    if (!s_initialized) return;

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    g_sleep_data.data_timestamp_ms = now_ms;

    audio_feature_t feat;
    memset(&feat, 0, sizeof(feat));
    bool got = audio_pipeline_get_feature(&feat);

    if (got && feat.audio_valid) {
        g_sleep_data.sensor.mic_online = true;
        g_sleep_data.sensor.last_mic_update_ms = feat.timestamp_ms;

        int db = (int)(feat.rms_energy * 0.5f);
        if (db > 100) db = 100;
        g_sleep_data.snore_db = db;
        if (db > s_max_snore_db) s_max_snore_db = db;
        g_sleep_data.max_snore_db = s_max_snore_db;

        if (feat.is_snoring && !s_last_snoring) g_sleep_data.snore_count++;
        s_last_snoring = feat.is_snoring;

        if      (feat.snore_prob > 0.7f) g_sleep_data.risk_level = RISK_HIGH;
        else if (feat.snore_prob > 0.4f) g_sleep_data.risk_level = RISK_MIDDLE;
        else if (feat.snore_prob > 0.1f) g_sleep_data.risk_level = RISK_LOW;
        else                              g_sleep_data.risk_level = RISK_NORMAL;

        /* ★ 传递 snore_classifier 分类结果 → SleepData_t */
        g_sleep_data.snore_type           = feat.snore_type;
        g_sleep_data.snore_type_confidence = feat.snore_type_confidence;

        g_sleep_data.system_state = SYS_STATE_MONITORING;

        int base = 85;
        if (feat.is_snoring) base -= 10;
        if (db > 60) base -= 5;
        if (feat.snore_prob > 0.5f) base -= 5;
        if (base < 30) base = 30;
        if (base > 95) base = 95;
        g_sleep_data.sleep_score = base;
    } else {
        g_sleep_data.sensor.mic_online = false;
        g_sleep_data.snore_db = 0;
        g_sleep_data.snore_type = 0;
        g_sleep_data.snore_type_confidence = 0.0f;
    }

    /* 未接硬件字段清零 */
    g_sleep_data.heart_rate  = 0;
    g_sleep_data.breath_rate = 0;
    g_sleep_data.spo2        = 0;
    g_sleep_data.movement_level = 0;
    g_sleep_data.in_bed      = false;
    g_sleep_data.temperature = 0.0f;
    g_sleep_data.humidity    = 0.0f;
    g_sleep_data.sensor.radar_online = false;
    g_sleep_data.sensor.sd_online    = false;
    g_sleep_data.sensor.wifi_connected = false;
    g_sleep_data.sensor.last_radar_update_ms = 0;
    g_sleep_data.sensor.last_env_update_ms   = 0;
    g_sleep_data.sleep_stage  = SLEEP_STAGE_AWAKE;
    g_sleep_data.posture      = POSTURE_SUPINE;
    g_sleep_data.comfort      = COMFORT_GOOD;
    g_sleep_data.apnea_count  = 0;
    g_sleep_data.turn_over_count = 0;
    g_sleep_data.min_spo2     = 0;
}

bool sleep_audio_adapter_ready(void)
{
    return s_initialized;
}
