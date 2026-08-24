/**
 * fusion_task.cc — 小智主板融合任务实现
 *
 * 每秒 tick，桥接 SNORE 接收 + R60 雷达 + 融合引擎 + 板载传感器 → SleepDataCenter
 */
#include "fusion_task.h"
#include "app_role_config.h"
#include "fusion_types.h"
#include "sleep_fusion.h"
#include "sleep_health_fusion.h"
#include "sleep_baseline.h"
#include "radar_breath_event.h"
#include "snore_event_detector.h"
#include "sleep_radar_data.h"
#include "snore_report_rx.h"
#include "r60abd1_adapter.h"
#include "r60abd1_uart.h"
#include "main_report_uart.h"
#include "sleep_data_center.h"

#if CONFIG_ENABLE_SHT30
#include "sensors/sht30.h"
#endif
#if CONFIG_ENABLE_AP3216C
#include "sensors/ap3216c.h"
#endif
#if CONFIG_ENABLE_CHIP_TEMP
#include "sensors/chip_temp.h"
#endif

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

static const char *TAG = "FUSION_TASK";

#define FUSION_TICK_MS      1000
#define FUSION_TASK_STACK   8192
#define FUSION_TASK_PRIO    3

#define SNORE_STALE_MS      3000   /* SNORE 超过 3 秒视为过期 */
#define RADAR_STALE_MS      5000   /* 雷达超过 5 秒视为过期 */

static TaskHandle_t s_fusion_task_handle = NULL;
static bool s_running = false;
static bool s_initialized = false;

/* ── SNORE 报告 → audio_feature_t 转换 ── */
static void snore_to_audio_feature(const snore_report_t *rpt, audio_feature_t *af)
{
    memset(af, 0, sizeof(*af));

    af->timestamp_ms = rpt->ts_ms;
    af->audio_valid  = rpt->audio_valid && rpt->mic_ok;
    af->mic_connected = rpt->mic_ok;
    af->feature_valid = rpt->audio_valid;
    af->model_enabled = true;          /* 鼾声模型在 UI 板上运行 */
    af->model_valid   = rpt->audio_valid;

    af->is_snoring    = rpt->snore_active;
    af->snore_prob    = rpt->snore_score / 255.0f;
    af->snore_type    = 0;             /* UI 板不区分类型，融合层判断 */

    af->rms_energy    = (float)rpt->rms;
    af->peak          = (int16_t)rpt->peak;
    af->zcr           = rpt->zcr_x100 / 100.0f;

    /* 从 quality 推算噪声相关指标 */
    af->noise_floor     = (float)rpt->snore_db;
    af->noise_too_high  = (rpt->quality < 30);
    af->inference_time_ms = 0;
}

/* ── 板载传感器读取 → SleepDataCenter ── */
#if CONFIG_ENABLE_ENV_SENSOR
static void read_onboard_sensors(void)
{
#if CONFIG_ENABLE_SHT30
    sht30_data_t sht;
    if (sht30_read(&sht) == ESP_OK) {
        SleepDataCenter::GetInstance().UpdateEnvironment(
            sht.temperature, sht.humidity, -1);
    }
#endif

#if CONFIG_ENABLE_AP3216C
    ap3216c_data_t als;
    if (ap3216c_read(&als) == ESP_OK) {
        SleepDataCenter::GetInstance().UpdateEnvironment(
            -999.0f, -999.0f, (int)als.als);
    }
#endif
}
#endif /* CONFIG_ENABLE_ENV_SENSOR */

/* ── 融合定时任务 ── */
static void fusion_tick_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Fusion tick task started, interval=%d ms", FUSION_TICK_MS);

    TickType_t last_wake = xTaskGetTickCount();

    while (s_running) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

        /* ====== 1. SNORE 音频特征 ====== */
        bool audio_valid = false;
        snore_report_t snore;
        if (snore_report_rx_get_latest(&snore) && snore.valid) {
            if (snore_report_is_fresh(SNORE_STALE_MS)) {
                audio_feature_t af;
                snore_to_audio_feature(&snore, &af);
                sleep_fusion_feed_audio(&af);

                /* 同步更新 SleepDataCenter 鼾声统计 */
                SleepDataCenter::GetInstance().UpdateSnore(
                    af.snore_prob, af.is_snoring, 1);
                SleepDataCenter::GetInstance().UpdateAudioSummary(
                    af.rms_energy, af.is_snoring ? 1 : 0);

                audio_valid = true;
            }
        }

        /* ====== 2. R60 雷达特征 ====== */
        bool radar_valid = false;
        sleep_radar_data_t radar_snapshot;
        if (sleep_radar_data_get_snapshot(&radar_snapshot)) {
            if (sleep_radar_data_is_fresh(radar_snapshot.last_update_ms, RADAR_STALE_MS)) {
                radar_feature_t rf;
                r60abd1_adapter_convert(&radar_snapshot, &rf);
                sleep_fusion_feed_radar(&rf);

                /* 同步更新 SleepDataCenter 雷达数据 */
                SleepDataCenter::GetInstance().UpdateRadar(
                    rf.presence,
                    rf.body_motion,
                    (int)rf.heart_bpm,
                    (int)rf.breath_bpm,
                    (int)rf.distance_cm);

                radar_valid = true;
            }
        }

        /* ====== 3. 融合引擎 tick ====== */
        sleep_fusion_tick();

        /* ====== 4. 健康融合 tick ====== */
        const baseline_t *bl = sleep_baseline_get();
        audio_feature_t af_latest;
        radar_feature_t rf_latest;
        /* 获取最新融合层数据用于健康分析 */
        {
            snore_report_t s;
            if (snore_report_rx_get_latest(&s) && s.valid) {
                snore_to_audio_feature(&s, &af_latest);
            } else {
                memset(&af_latest, 0, sizeof(af_latest));
            }
            if (sleep_radar_data_get_snapshot(&radar_snapshot)) {
                r60abd1_adapter_convert(&radar_snapshot, &rf_latest);
            } else {
                memset(&rf_latest, 0, sizeof(rf_latest));
            }
        }
        sleep_health_fusion_tick(&af_latest, &rf_latest,
                                  sleep_baseline_is_ready() ? bl : NULL,
                                  now_ms);

        /* ====== 5. 读取融合结果 → SleepDataCenter ====== */
        fusion_result_t fr;
        if (sleep_fusion_get_result(&fr)) {
            /* 更新生命体征到 SleepDataCenter */
            if (fr.data_valid) {
                SleepDataCenter::GetInstance().UpdateRadar(
                    fr.presence, fr.body_motion,
                    (int)fr.heart_bpm, (int)fr.breath_bpm,
                    (int)fr.distance_cm);

                if (fr.snoring) {
                    SleepDataCenter::GetInstance().AddSnoreEvent(
                        fr.timestamp_ms,
                        fr.duration_sec * 1000,
                        fr.snore_prob,
                        0);
                }
            }
        }

#if CONFIG_ENABLE_ENV_SENSOR
        /* ====== 6. 板载传感器（每 5 秒读一次） ====== */
        static int sensor_tick = 0;
        if (++sensor_tick >= 5) {
            sensor_tick = 0;
            read_onboard_sensors();
        }
#endif

        /* ====== 7. 发送 MAIN 给 UI 板 ====== */
        main_report_uart_send();

        /* ====== 8. 日志 ====== */
        ESP_LOGI(TAG, "[FUSION] audio_valid=%d radar_valid=%d event=%s risk=%d",
                 audio_valid, radar_valid,
                 sleep_fusion_event_label(fr.event),
                 fr.severity);

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(FUSION_TICK_MS));
    }

    ESP_LOGI(TAG, "Fusion tick task exiting");
    s_fusion_task_handle = NULL;
    vTaskDelete(NULL);
}

/* ── 公开接口 ── */

bool fusion_task_init(void)
{
    if (s_initialized) return true;

    /* 雷达数据结构初始化 */
    sleep_radar_data_init();

    /* 融合子模块初始化 */
    sleep_baseline_init();
    radar_breath_event_init();
    snore_event_detector_init();

    /* 融合引擎：无回调，主循环轮询获取结果 */
    if (sleep_fusion_init(NULL, NULL) != 0) {
        ESP_LOGE(TAG, "sleep_fusion_init failed");
        return false;
    }
    sleep_health_fusion_init();

    /* MAIN → UI 板发送初始化 */
    main_report_uart_init();

#if CONFIG_ENABLE_ENV_SENSOR
    /* 板载传感器初始化（非关键，失败不阻塞） */
#if CONFIG_ENABLE_SHT30
    if (sht30_init() != ESP_OK) {
        ESP_LOGW(TAG, "SHT30 not found, temp/humidity unavailable");
    }
#endif
#if CONFIG_ENABLE_AP3216C
    if (ap3216c_init() != ESP_OK) {
        ESP_LOGW(TAG, "AP3216C not found, ambient light unavailable");
    }
#endif
#if CONFIG_ENABLE_CHIP_TEMP
    chip_temp_init();
#endif
#endif /* CONFIG_ENABLE_ENV_SENSOR */

    s_initialized = true;
    ESP_LOGI(TAG, "Fusion engine init OK");
    return true;
}

bool fusion_task_start(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Not initialized, call fusion_task_init() first");
        return false;
    }
    if (s_running) return true;

    /* 启动 R60 UART */
    if (r60abd1_uart_init() != ESP_OK) {
        ESP_LOGE(TAG, "R60 UART init failed");
        return false;
    }
    if (r60abd1_uart_start_task() != ESP_OK) {
        ESP_LOGE(TAG, "R60 UART task start failed");
        return false;
    }

    s_running = true;
    BaseType_t ret = xTaskCreate(fusion_tick_task, "fusion_tick",
                                  FUSION_TASK_STACK, NULL,
                                  FUSION_TASK_PRIO, &s_fusion_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Task create failed");
        s_running = false;
        return false;
    }

    ESP_LOGI(TAG, "Fusion task started");
    return true;
}

void fusion_task_stop(void)
{
    s_running = false;

    r60abd1_uart_stop_task();

    if (s_fusion_task_handle) {
        /* 等待任务退出 */
        int wait = 0;
        while (s_fusion_task_handle && wait < 500) {
            vTaskDelay(pdMS_TO_TICKS(50));
            wait += 50;
        }
    }
    s_fusion_task_handle = NULL;

    ESP_LOGI(TAG, "Fusion task stopped");
}
