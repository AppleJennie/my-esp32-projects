/**
 * sleep_monitor_data_adapter.c — 统一数据适配层 v2
 *
 * 数据流:
 *   INMP441 → audio_pipeline → audio_feature_t
 *   R60ABD1 → r60abd1_uart → sleep_radar_data → r60abd1_adapter → radar_feature_t
 *   audio_feature_t + radar_feature_t → sleep_fusion → fusion_result_t
 *   fusion_result_t → sleep_health_fusion → health_fusion_result_t
 *   fusion_result_t + SleepData_t 映射 → sleep_ui_refresh()
 */
#include "sleep_monitor_data_adapter.h"
#include "sleep_project_config.h"
#include "app_role_config.h"
#include "sleep_data.h"
#include "inmp441_i2s.h"
#include "audio_pipeline.h"
#include "fusion_types.h"
#include "watch_ble_client.h"
#if CONFIG_ENABLE_SLEEP_FUSION
#include "sleep_fusion.h"
#endif
#if CONFIG_ENABLE_HEALTH_FUSION
#include "sleep_health_fusion.h"
#endif
#if CONFIG_ENABLE_BASELINE
#include "sleep_baseline.h"
#endif
#if CONFIG_ENABLE_R60_RADAR
#include "r60abd1_uart.h"
#include "r60abd1_adapter.h"
#include "sleep_radar_data.h"
#include "sleep_algorithm.h"
#endif
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>
#if CONFIG_ENABLE_SNORE_UART_REPORT
#include "snore_report_uart.h"
#endif
#if CONFIG_ENABLE_MAIN_REPORT_RX
#include "main_report_rx.h"
#endif

static const char *TAG = "MONITOR_DATA";

static bool s_initialized  = false;
static uint32_t s_monitor_start_ms = 0;

/* ── 融合回调结果缓存 ── */
#if CONFIG_ENABLE_SLEEP_FUSION
static fusion_result_t s_fusion_result;
#endif

/* ── 鼾声上升沿 ── */
static bool s_last_snoring_for_count = false;

/* ── debug 日志节流 ── */
#if CONFIG_ENABLE_SLEEP_FUSION || CONFIG_ENABLE_BASELINE
static uint32_t s_last_fusion_log_ms = 0;
#endif
static uint32_t s_last_data_log_ms   = 0;

/* ── INMP441 连续无效计数 ── */
static int s_mic_low_streak = 0;

#define WATCH_DATA_TIMEOUT_MS 15000

/* ── 鼾声事件统计内部状态：总次数、类型次数、时长统一在这里维护 ── */
static uint32_t s_snore_episode_start_ms = 0;
static uint32_t s_snore_episode_last_update_ms = 0;
static bool     s_snore_episode_type_counted = false;

static uint8_t normalize_snore_type(int t)
{
    /* 0=none, 1=nasal, 2=throat, 3=mouth, 4=mixed, 5=unknown */
    if (t >= 1 && t <= 4) return (uint8_t)t;
    if (t == 5) return 5;
    return 0;
}

static int confidence_to_percent(float conf)
{
    int pct;
    if (conf <= 1.0f) pct = (int)(conf * 100.0f + 0.5f);
    else              pct = (int)(conf + 0.5f);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

static uint8_t clamp_u8_from_int(int v, int lo, int hi)
{
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return (uint8_t)v;
}

static void add_snore_type_ms(uint8_t type, uint32_t delta_ms)
{
    switch (type) {
        case 1: g_sleep_data.nasal_snore_ms  += delta_ms; break;
        case 2: g_sleep_data.throat_snore_ms += delta_ms; break;
        case 3: g_sleep_data.mouth_snore_ms  += delta_ms; break;
        case 4: g_sleep_data.mixed_snore_ms  += delta_ms; break;
        default: break;
    }
}

#if CONFIG_ENABLE_SLEEP_FUSION
static void fusion_cb(const fusion_result_t *r, void *user)
{
    if (r) memcpy(&s_fusion_result, r, sizeof(fusion_result_t));
}
#endif

/* ═══════════════════════════════════════════════════════════════
 * INIT
 * ═══════════════════════════════════════════════════════════════ */

esp_err_t sleep_monitor_data_adapter_init(void)
{
    ESP_LOGI(TAG, "=== Monitor Data Adapter Init ===");

    /* ── 融合引擎 ── */
#if CONFIG_ENABLE_BASELINE
    sleep_baseline_init();
#endif
#if CONFIG_ENABLE_SLEEP_FUSION
    sleep_fusion_init(fusion_cb, NULL);
#endif
#if CONFIG_ENABLE_HEALTH_FUSION
    sleep_health_fusion_init();
#endif
#if CONFIG_ENABLE_SLEEP_FUSION || CONFIG_ENABLE_HEALTH_FUSION || CONFIG_ENABLE_BASELINE
    ESP_LOGI(TAG, "[1/3] Fusion engine init OK (baseline+fusion+health)");
#else
    ESP_LOGI(TAG, "[1/3] Fusion engine disabled — using UART MAIN data from other board");
#endif

    /* ── R60 雷达 ── */
#if CONFIG_ENABLE_R60_RADAR
    sleep_radar_data_init();
    esp_err_t ret = r60abd1_uart_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "R60 UART init FAIL (%s) — radar disabled", esp_err_to_name(ret));
    } else {
        ret = r60abd1_uart_start_task();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "R60 task start FAIL");
        } else {
            s_radar_ok = true;
            ESP_LOGI(TAG, "[2/3] R60 UART task started (CPU1, prio=8)");
            BaseType_t qok = xTaskCreatePinnedToCore(
                r60abd1_query_task, "r60_query", 4096, NULL, 4, NULL, 1);
            if (qok == pdPASS) { s_query_started = true; }
        }
    }
#else
    ESP_LOGI(TAG, "[2/3] R60 radar disabled in config");
#endif

    /* ── UART 双板通讯 ── */
#if CONFIG_ENABLE_SNORE_UART_REPORT
    snore_report_uart_init();
#endif
#if CONFIG_ENABLE_MAIN_REPORT_RX
    main_report_rx_init();
#endif
#if CONFIG_ENABLE_SNORE_UART_REPORT || CONFIG_ENABLE_MAIN_REPORT_RX
    ESP_LOGI(TAG, "[UART] Shared UART2 init OK TX=%d RX=%d <-> other board",
             SHARED_UART_TXD_GPIO, SHARED_UART_RXD_GPIO);
#endif

    s_initialized = true;
    ESP_LOGI(TAG, "[3/3] === Monitor Data Adapter Ready ===");
    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════════════
 * UPDATE — LVGL 主循环每 500ms 调用
 * ═══════════════════════════════════════════════════════════════ */

void sleep_monitor_data_adapter_update(void)
{
    if (!s_initialized) return;

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    g_sleep_data.data_timestamp_ms = now_ms;

    /* ── 接收对端主板 MAIN 回传 ── */
#if CONFIG_ENABLE_MAIN_REPORT_RX
    main_report_rx_poll();
#endif

    /* ── 读取音频特征 ── */
    audio_feature_t afeat;
    memset(&afeat, 0, sizeof(afeat));
    bool audio_got = audio_pipeline_get_feature(&afeat);
    if (audio_got) {
        /* 计算特征年龄 */
        if (afeat.timestamp_ms > 0 && now_ms > afeat.timestamp_ms) {
            afeat.model_age_ms = now_ms - afeat.timestamp_ms;
        } else {
            afeat.model_age_ms = 0;
        }
    }

    /* ── INMP441 有效性检测：rms=1 peak=1 视为硬件断开/故障 ── */
    bool mic_data_ok = audio_got && afeat.audio_valid;
    if (mic_data_ok) {
        /* rms_energy 约等于 1.0 且 peak 为 1 → INMP441 未接或无信号 */
        if (afeat.rms_energy < 2.0f && afeat.peak <= 1) {
            s_mic_low_streak++;
            if (s_mic_low_streak >= 3) {
                mic_data_ok = false;  /* 连续 3 次无效 → 标记为无效 */
            }
        } else {
            s_mic_low_streak = 0;
        }
    } else {
        s_mic_low_streak++;
    }

    /* ── 读取雷达特征 ── */
    radar_feature_t rfeat;
    memset(&rfeat, 0, sizeof(rfeat));
    bool radar_got = false;
#if CONFIG_ENABLE_R60_RADAR
    if (s_radar_ok) {
        sleep_radar_data_t *r = sleep_radar_data_get();
        if (r) {
            r60abd1_adapter_convert(r, &rfeat);
            radar_got = rfeat.radar_connected;
        }
    }
#endif

    /* ── 喂融合引擎 ── */
#if CONFIG_ENABLE_SLEEP_FUSION
    if (mic_data_ok) {
        sleep_fusion_feed_audio(&afeat);
    }
    if (radar_got) {
        sleep_fusion_feed_radar(&rfeat);
    }
#endif

    /* ── 每秒 tick ── */
    bool one_sec_tick = false;
    {
        static uint32_t last_tick = 0;
        if (now_ms - last_tick >= 1000) {
            last_tick = now_ms;
            one_sec_tick = true;
#if CONFIG_ENABLE_SLEEP_FUSION
            sleep_fusion_tick();
#endif
#if CONFIG_ENABLE_HEALTH_FUSION
            {
                const baseline_t *bl = NULL;
#if CONFIG_ENABLE_BASELINE
                bl = sleep_baseline_get();
#endif
                sleep_health_fusion_tick(
                    mic_data_ok ? &afeat : NULL,
                    radar_got ? &rfeat : NULL,
                    bl, now_ms);
            }
#endif
        }
    }

    /* ── 获取融合结果 ── */
    fusion_result_t fr;
    memset(&fr, 0, sizeof(fr));
    bool fusion_ok = false;
#if CONFIG_ENABLE_SLEEP_FUSION
    fusion_ok = sleep_fusion_get_result(&fr);
    if (!fusion_ok) {
        memcpy(&fr, &s_fusion_result, sizeof(fr));
    }
#endif

    /* ═══ 映射到 SleepData_t ═══ */

    /* 音频 — 当前状态、类型、声学指标 */
    bool now_snoring = false;
    uint8_t current_snore_type = 0;

    if (mic_data_ok) {
        g_sleep_data.sensor.mic_online = true;
        g_sleep_data.sensor.last_mic_update_ms = afeat.timestamp_ms;

        now_snoring = (afeat.model_snoring || afeat.is_snoring || afeat.snore_prob >= 0.55f);
        current_snore_type = normalize_snore_type(afeat.snore_type);

        if (now_snoring) {
            /* dBFS: 20*log10(rms/32768) + 90, clamp 0..100 */
            float ratio = afeat.rms_energy / 32768.0f;
            if (ratio < 1e-6f) ratio = 1e-6f;
            int db = (int)(20.0f * log10f(ratio) + 90.0f);
            if (db > 100) db = 100;
            if (db < 1)   db = 1;
            g_sleep_data.snore_db = db;
            static int max_db = 0;
            if (db > max_db) max_db = db;
            g_sleep_data.max_snore_db = max_db;
        } else {
            g_sleep_data.snore_db = 0;
            current_snore_type = 0;
        }

        /* 当前类型与声学特征全部从 audio_feature_t 来，避免 audio_pipeline 直接写 UI 全局变量。 */
        g_sleep_data.snore_type            = current_snore_type;
        g_sleep_data.snore_type_confidence = now_snoring ? confidence_to_percent(afeat.snore_type_confidence) : 0;
        g_sleep_data.spectral_centroid_hz  = (int)afeat.spectral_centroid;
        g_sleep_data.low_freq_ratio_x100   = (int)(afeat.low_freq_ratio * 100.0f + 0.5f);
        g_sleep_data.harmonic_ratio_x100   = (int)(afeat.harmonic_ratio * 100.0f + 0.5f);
        g_sleep_data.airflow_sound_present = afeat.airflow_sound_present ? 1 : 0;
        g_sleep_data.recovery_breath_sound = afeat.recovery_breath_sound ? 1 : 0;
        if (g_sleep_data.low_freq_ratio_x100 < 0) g_sleep_data.low_freq_ratio_x100 = 0;
        if (g_sleep_data.low_freq_ratio_x100 > 100) g_sleep_data.low_freq_ratio_x100 = 100;
        if (g_sleep_data.harmonic_ratio_x100 < 0) g_sleep_data.harmonic_ratio_x100 = 0;
        if (g_sleep_data.harmonic_ratio_x100 > 100) g_sleep_data.harmonic_ratio_x100 = 100;
    } else {
        g_sleep_data.sensor.mic_online = false;
        g_sleep_data.snore_db = 0;
        g_sleep_data.snore_type = 0;
        g_sleep_data.snore_type_confidence = 0;
        g_sleep_data.spectral_centroid_hz = 0;
        g_sleep_data.low_freq_ratio_x100 = 0;
        g_sleep_data.harmonic_ratio_x100 = 0;
        g_sleep_data.airflow_sound_present = 0;
        g_sleep_data.recovery_breath_sound = 0;
        now_snoring = false;
        current_snore_type = 0;
    }

    /* 鼾声事件统计 — 总次数、类型次数、总时长从同一个上升沿/片段更新 */
    {
        if (now_snoring) {
            if (!s_last_snoring_for_count) {
                g_sleep_data.snore_count++;
                g_sleep_data.current_snore_episode_ms = 0;
                g_sleep_data.last_snore_event_type = 0;
                s_snore_episode_start_ms = now_ms;
                s_snore_episode_last_update_ms = now_ms;
                s_snore_episode_type_counted = false;
            }

            if (s_snore_episode_start_ms == 0) {
                s_snore_episode_start_ms = now_ms;
                s_snore_episode_last_update_ms = now_ms;
            }

            uint32_t delta_ms = 0;
            if (s_snore_episode_last_update_ms > 0 && now_ms >= s_snore_episode_last_update_ms) {
                delta_ms = now_ms - s_snore_episode_last_update_ms;
                if (delta_ms > 1500) delta_ms = 1000;  /* 阻止卡顿后一次性暴涨 */
            }
            s_snore_episode_last_update_ms = now_ms;

            g_sleep_data.current_snore_episode_ms = now_ms - s_snore_episode_start_ms;
            g_sleep_data.snore_total_ms += delta_ms;

            if (current_snore_type >= 1 && current_snore_type <= 4) {
                add_snore_type_ms(current_snore_type, delta_ms);

                if (!s_snore_episode_type_counted) {
                    g_sleep_data.snore_type_count[current_snore_type]++;
                    g_sleep_data.last_snore_event_type = current_snore_type;
                    s_snore_episode_type_counted = true;
                }
            }
        } else {
            if (s_last_snoring_for_count) {
                if (g_sleep_data.current_snore_episode_ms > g_sleep_data.longest_snore_episode_ms) {
                    g_sleep_data.longest_snore_episode_ms = g_sleep_data.current_snore_episode_ms;
                }

                /* 整个片段结束仍没有有效类型，则归为 unknown，保证总次数能对得上。 */
                if (!s_snore_episode_type_counted) {
                    g_sleep_data.snore_type_count[5]++;
                    g_sleep_data.last_snore_event_type = 5;
                }
            }

            g_sleep_data.current_snore_episode_ms = 0;
            s_snore_episode_start_ms = 0;
            s_snore_episode_last_update_ms = 0;
            s_snore_episode_type_counted = false;
        }

        s_last_snoring_for_count = now_snoring;
    }

#if CONFIG_ENABLE_SNORE_UART_REPORT
    /* ── 每秒发送鼾声报告到对端主板：必须放在本地映射/计数之后，避免上报旧数据 ── */
    if (one_sec_tick) {
        snore_report_t rpt;
        memset(&rpt, 0, sizeof(rpt));
        rpt.ts_ms       = now_ms;
        rpt.mic_ok      = mic_data_ok ? 1 : 0;
        rpt.audio_valid  = mic_data_ok ? 1 : 0;
        rpt.snore_active = now_snoring ? 1 : 0;
        rpt.snore_score  = clamp_u8_from_int((int)(afeat.snore_prob * 255.0f + 0.5f), 0, 255);
        rpt.snore_db     = clamp_u8_from_int(g_sleep_data.snore_db, 0, 100);
        rpt.rms          = (uint16_t)(afeat.rms_energy < 0 ? 0 : (afeat.rms_energy > 65535 ? 65535 : afeat.rms_energy));
        rpt.peak         = (uint16_t)(afeat.peak < 0 ? 0 : afeat.peak);
        rpt.zcr_x100     = clamp_u8_from_int((int)(afeat.zcr * 100.0f + 0.5f), 0, 100);
        rpt.current_episode_ms = g_sleep_data.current_snore_episode_ms;
        rpt.snore_total_ms     = g_sleep_data.snore_total_ms;
        rpt.longest_episode_ms = g_sleep_data.longest_snore_episode_ms;
        rpt.snore_episode_count = (uint16_t)(g_sleep_data.snore_count < 0 ? 0 :
                                      (g_sleep_data.snore_count > 65535 ? 65535 : g_sleep_data.snore_count));
        rpt.data_quality = mic_data_ok ? 0 : 1;

        /* v2 新增 7 字段: 声学线索,非医学诊断 */
        rpt.snore_type            = (uint8_t)g_sleep_data.snore_type;
        rpt.type_confidence        = (uint8_t)g_sleep_data.snore_type_confidence;
        rpt.spectral_centroid_hz   = (uint16_t)(g_sleep_data.spectral_centroid_hz < 0 ? 0 : g_sleep_data.spectral_centroid_hz);
        rpt.low_freq_ratio_x100    = clamp_u8_from_int(g_sleep_data.low_freq_ratio_x100, 0, 100);
        rpt.harmonic_ratio_x100    = clamp_u8_from_int(g_sleep_data.harmonic_ratio_x100, 0, 100);
        rpt.airflow_sound_present  = (uint8_t)g_sleep_data.airflow_sound_present;
        rpt.recovery_breath_sound  = (uint8_t)g_sleep_data.recovery_breath_sound;
        snore_report_uart_send(&rpt);
    }
#endif

    /* 雷达生命体征 */
    if (radar_got && rfeat.radar_connected) {
        g_sleep_data.sensor.radar_online = true;
        g_sleep_data.sensor.last_radar_update_ms = now_ms;
        if (rfeat.heart_valid)  g_sleep_data.heart_rate = (int)rfeat.heart_bpm;
        if (rfeat.breath_valid) g_sleep_data.breath_rate = (int)rfeat.breath_bpm;
        if (rfeat.motion_valid) g_sleep_data.movement_level = rfeat.body_motion;
        g_sleep_data.in_bed = rfeat.in_bed;
    } else {
        g_sleep_data.sensor.radar_online = false;
        g_sleep_data.heart_rate  = 0;
        g_sleep_data.breath_rate = 0;
        g_sleep_data.movement_level = 0;
        g_sleep_data.in_bed = false;
    }

    /* ── 融合评分映射 ── */
    if (fusion_ok || fr.timestamp_ms > 0) {
        /* ── 睡眠评分 ── */
        int fused_score = 50;
        if (mic_data_ok && fr.total_score > 0) {
            fused_score = fr.total_score;
        } else if (mic_data_ok) {
            /* 音频有效但融合评分未就绪 → 基于音频的简单打分 */
            int base = 85;
            if (afeat.is_snoring) base -= 10;
            if (g_sleep_data.snore_db > 60) base -= 5;
            if (afeat.snore_prob > 0.5f) base -= 5;
            if (base < 30) base = 30;
            if (base > 95) base = 95;
            fused_score = base;
        }
        g_sleep_data.sleep_score = fused_score;
        if (g_sleep_data.sleep_score > 100) g_sleep_data.sleep_score = 100;
        if (g_sleep_data.sleep_score < 0)  g_sleep_data.sleep_score = 0;

        /* ── 风险等级 ── */
        /* audio-only 模式下不输出高置信风险 */
        if (!radar_got) {
            /* 无雷达时最多给 LOW 风险，不做强结论 */
            if (mic_data_ok && afeat.snore_prob > 0.7f)
                g_sleep_data.risk_level = RISK_LOW;
            else
                g_sleep_data.risk_level = RISK_NORMAL;
        } else if (fr.severity >= 3)       g_sleep_data.risk_level = RISK_HIGH;
        else if (fr.severity == 2)         g_sleep_data.risk_level = RISK_MIDDLE;
        else if (fr.severity == 1)         g_sleep_data.risk_level = RISK_LOW;
        else                               g_sleep_data.risk_level = RISK_NORMAL;

        /* ── system_state 映射 ── */
        if (radar_got && rfeat.radar_connected && rfeat.in_bed)
            g_sleep_data.system_state = SYS_STATE_SLEEPING;
        else if (mic_data_ok)
            g_sleep_data.system_state = SYS_STATE_AUDIO_ONLY;
        else if (radar_got && rfeat.radar_connected)
            g_sleep_data.system_state = SYS_STATE_MONITORING;
        else
            g_sleep_data.system_state = SYS_STATE_STANDBY;

        /* ── apnea 计数 — 仅雷达有效时才赋值 ── */
        if (radar_got) {
            g_sleep_data.apnea_count = fr.suspected_apnea_count;
        }

        /* ── 监护时长 ── */
        if (g_sleep_data.system_state >= SYS_STATE_AUDIO_ONLY) {
            if (s_monitor_start_ms == 0) s_monitor_start_ms = now_ms;
            g_sleep_data.monitor_duration_sec = (now_ms - s_monitor_start_ms) / 1000;
        } else {
            s_monitor_start_ms = 0;
            g_sleep_data.monitor_duration_sec = 0;
        }
    }

    /* ═══ 主板 MAIN 回传数据映射 ═══ */
#if CONFIG_ENABLE_MAIN_REPORT_RX
    {
        main_report_t mr;
        bool mr_ok = main_report_rx_get_latest(&mr) && mr.valid;
        bool main_timeout = main_report_rx_is_timeout(now_ms);
        g_sleep_data.main_online = mr_ok && !main_timeout;

        if (g_sleep_data.main_online) {
            /* 雷达/传感器在线状态 */
            g_sleep_data.sensor.radar_online = (mr.radar_status == 2);
            g_sleep_data.sensor.last_radar_update_ms = mr.rx_ms;
            g_sleep_data.sensor.wifi_connected = mr.wifi_ok;
            g_sleep_data.sensor.sd_online = mr.sd_ok;

            /* 生命体征 — 仅 valid=1 时写入 */
            if (mr.heart_valid)  g_sleep_data.heart_rate   = mr.heart_bpm;
            else                 g_sleep_data.heart_rate   = 0;
            if (mr.breath_valid) g_sleep_data.breath_rate  = mr.breath_bpm;
            else                 g_sleep_data.breath_rate  = 0;
            if (mr.spo2_valid) {
                g_sleep_data.spo2 = mr.spo2;
                g_sleep_data.spo2_from_watch = false;
                if (g_sleep_data.min_spo2 <= 0 || mr.spo2 < g_sleep_data.min_spo2) {
                    g_sleep_data.min_spo2 = mr.spo2;
                }
            } else {
                g_sleep_data.spo2 = 0;
                g_sleep_data.spo2_from_watch = false;
            }
            if (mr.temp_valid)   g_sleep_data.temperature  = mr.temp_x10 / 10.0f;
            else                 g_sleep_data.temperature  = 0.0f;
            g_sleep_data.humidity    = mr.hum_x10 / 10.0f;

            /* 体动/在床/阶段 */
            if (mr.motion_valid) g_sleep_data.movement_level = mr.body_motion;
            if (mr.bed_valid)    g_sleep_data.in_bed         = mr.in_bed;
            if (mr.stage_valid && mr.sleep_stage <= 3)
                g_sleep_data.sleep_stage = (SleepStage_t)mr.sleep_stage;

            /* 风险/事件 */
            if (mr.risk_valid) {
                g_sleep_data.risk_level    = (SleepRisk_t)mr.risk_level;
                g_sleep_data.apnea_count   = mr.apnea_count;
                g_sleep_data.hypopnea_count = mr.hypopnea_count;
            }
            g_sleep_data.event_id          = mr.event_id;
            g_sleep_data.system_status     = mr.system_status;

            /* v2 扩展字段 */
            g_sleep_data.has_main_ext = mr.has_main_ext;
            if (mr.has_main_ext) {
                g_sleep_data.breath_amp        = mr.breath_amp;
                g_sleep_data.baseline_ok       = mr.baseline_ok;
                g_sleep_data.baseline_progress  = mr.baseline_progress;
                g_sleep_data.event_confidence   = mr.event_confidence;
                g_sleep_data.distance_cm        = mr.distance_cm;
                g_sleep_data.snore_resp_score   = mr.snore_resp_score;
            }
        } else {
            /* 主板离线: 清零依赖MAIN的字段, 但保留BLE血氧 */
            g_sleep_data.heart_rate   = 0;
            g_sleep_data.breath_rate  = 0;
            g_sleep_data.temperature  = 0.0f;
            g_sleep_data.humidity     = 0.0f;
            g_sleep_data.in_bed       = false;
            g_sleep_data.has_main_ext = false;
        }
    }
#endif

    /* ═══ BLE手表数据兜底融合 ═══
     * 手表数据不在 BLE 任务里直接覆盖 UI 全局变量，而是在适配层统一仲裁：
     * 1) MAIN 有有效血氧时以 MAIN 为准；
     * 2) MAIN 无血氧或主板离线时，用 BLE 手表 SpO2 兜底；
     * 3) 心率只在 MAIN/雷达无有效心率时兜底，避免多源互相覆盖。
     */
    {
        watch_ble_latest_t wd;
        bool wd_ok = watch_ble_client_get_latest(&wd);
        bool ble_connected = watch_ble_is_connected();
        bool wd_fresh = wd_ok && wd.last_update_ms > 0 &&
                        (now_ms - wd.last_update_ms <= WATCH_DATA_TIMEOUT_MS);

        g_sleep_data.watch_online = ble_connected;
        if (wd_fresh) {
            g_sleep_data.last_watch_update_ms = wd.last_update_ms;
        }

        if (wd_fresh && wd.has_spo2 && wd.spo2 >= 50 && wd.spo2 <= 100) {
            bool main_has_spo2 = g_sleep_data.main_online && g_sleep_data.spo2 > 0 && !g_sleep_data.spo2_from_watch;
            g_sleep_data.watch_spo2_valid = true;
            g_sleep_data.watch_spo2 = wd.spo2;
            g_sleep_data.watch_spo2_is_reference = wd.spo2_is_reference;

            if (!main_has_spo2) {
                g_sleep_data.spo2 = wd.spo2;
                g_sleep_data.spo2_from_watch = true;
            }
            if (g_sleep_data.min_spo2 <= 0 || wd.spo2 < g_sleep_data.min_spo2) {
                g_sleep_data.min_spo2 = wd.spo2;
            }
        } else if (!wd_fresh) {
            g_sleep_data.watch_spo2_valid = false;
            g_sleep_data.spo2_from_watch = false;
        }

        if (wd_fresh && wd.has_hr && wd.hr >= 30 && wd.hr <= 220) {
            g_sleep_data.watch_hr_valid = true;
            g_sleep_data.watch_hr = wd.hr;
            if (g_sleep_data.heart_rate <= 0) {
                g_sleep_data.heart_rate = wd.hr;
            }
        } else if (!wd_fresh) {
            g_sleep_data.watch_hr_valid = false;
        }

        if (wd_fresh) {
            if (wd.has_step) g_sleep_data.watch_step = wd.step;
            if (wd.has_posture) g_sleep_data.watch_posture = wd.posture;
        }
    }

    /* ═══ 未接硬件字段 ═══ */
#if !CONFIG_ENABLE_MAIN_REPORT_RX
    if (!g_sleep_data.watch_spo2_valid) {
        g_sleep_data.spo2 = 0;
    }
    g_sleep_data.temperature = 0.0f;
    g_sleep_data.humidity    = 0.0f;
#endif
    g_sleep_data.sensor.sd_online = false;
    /* wifi_connected already handled above */

    /* ── 融合调试日志 — 每 5 秒打印一次 ── */
#if CONFIG_ENABLE_SLEEP_FUSION || CONFIG_ENABLE_BASELINE
    if (now_ms - s_last_fusion_log_ms >= 5000) {
        s_last_fusion_log_ms = now_ms;
#if CONFIG_ENABLE_SLEEP_FUSION
        const char *evt_name = sleep_fusion_event_label(fr.event);
#else
        const char *evt_name = "disabled";
#endif
#if CONFIG_ENABLE_BASELINE
        const baseline_t *bl = sleep_baseline_get();
#else
        const baseline_t *bl = NULL;
#endif
        ESP_LOGI(TAG,
            "[FUSION] audio_valid=%d radar_valid=%d baseline_ready=%d "
            "snore=%d event=%s score=%d severity=%d",
            mic_data_ok ? 1 : 0,
            radar_got ? 1 : 0,
            (bl && bl->valid) ? 1 : 0,
            g_sleep_data.snore_count,
            evt_name,
            g_sleep_data.sleep_score,
            fr.severity);
    }
#endif

    if (now_ms - s_last_data_log_ms >= 5000) {
        s_last_data_log_ms = now_ms;
        ESP_LOGI(TAG,
            "[DATA] score=%d snore_db=%d snore_count=%d snore_type=%d "
            "type_cnt[N=%lu,T=%lu,M=%lu,X=%lu,U=%lu] hr=%d br=%d spo2=%d%s ble=%s mic=%s radar=%s",
            g_sleep_data.sleep_score,
            g_sleep_data.snore_db,
            g_sleep_data.snore_count,
            g_sleep_data.snore_type,
            (unsigned long)g_sleep_data.snore_type_count[1],
            (unsigned long)g_sleep_data.snore_type_count[2],
            (unsigned long)g_sleep_data.snore_type_count[3],
            (unsigned long)g_sleep_data.snore_type_count[4],
            (unsigned long)g_sleep_data.snore_type_count[5],
            g_sleep_data.heart_rate,
            g_sleep_data.breath_rate,
            g_sleep_data.spo2,
            g_sleep_data.spo2_from_watch ? "(BLE)" : "",
            g_sleep_data.watch_online ? "OK" : "OFF",
            g_sleep_data.sensor.mic_online ? "OK" : "LOW",
            g_sleep_data.sensor.radar_online ? "OK" : "OFF");

#if CONFIG_ENABLE_HEALTH_FUSION
        const health_fusion_result_t *hr = sleep_health_fusion_get();
        /* [HEALTH] 日志 */
        if (hr) {
            float ahi = 0.0f;
            uint32_t dur_s = g_sleep_data.monitor_duration_sec;
            if (dur_s > 0) {
                ahi = (float)(hr->pause_suspected_events) / ((float)dur_s / 3600.0f);
            }
            ESP_LOGI(TAG,
                "[HEALTH] risk=%s snore_class=%s apnea=%lu hypopnea=%lu ahi=%.1f",
                health_risk_label(hr->health_risk),
                snore_class_label(hr->snore_class),
                (unsigned long)hr->pause_suspected_events,
                (unsigned long)hr->shallow_breath_events,
                ahi);
        }
#else
        ESP_LOGI(TAG,
            "[HEALTH] disabled — using MAIN report from other board");
#endif
    }
}

bool sleep_monitor_data_adapter_ready(void)
{
    return s_initialized;
}
