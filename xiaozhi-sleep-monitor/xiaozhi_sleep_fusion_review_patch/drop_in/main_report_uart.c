/**
 * main_report_uart.c — 小智主板 MAIN 数据发送实现（快照读取版）
 */
#include "main_report_uart.h"
#include "app_role_config.h"
#include "fusion_types.h"
#include "sleep_fusion.h"
#include "sleep_health_fusion.h"
#include "sleep_radar_data.h"
#include "snore_report_rx.h"

#include <stdio.h>
#include <string.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>

static const char *TAG = "MAIN_TX";

#define MAIN_TX_BUF_SIZE  256

static uint32_t s_send_count = 0;
static uint32_t s_last_stat_ms = 0;

static int get_radar_status(const sleep_radar_data_t *r)
{
    if (!r || !r->radar_connected || r->last_update_ms == 0) return 0;

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
    if ((uint32_t)(now - r->last_update_ms) >= 5000U) return 3;

    if (r->heart_rate > 0 || r->breath_rate > 0 || r->body_motion > 0 ||
        r->last_heart_rate_ms > 0 || r->last_breath_rate_ms > 0 ||
        r->last_presence_ms > 0 || r->last_breath_wave_ms > 0) {
        return 2;
    }
    return 1;
}

static int get_system_status(bool audio_valid, int radar_status)
{
    bool radar_ok = (radar_status == 2);
    if (audio_valid && radar_ok) return 2;   /* 融合中 */
    if (audio_valid && !radar_ok) return 0;  /* 仅音频 */
    if (!audio_valid && radar_ok) return 2;  /* 雷达在线，UI 仍可显示体征 */
    if (radar_status == 1) return 1;         /* 等待雷达稳定 */
    return 3;                                /* 数据质量低/主板无有效输入 */
}

static int health_risk_to_level(health_risk_t r)
{
    switch (r) {
        case HLTH_RISK_NORMAL:             return 0;
        case HLTH_RISK_LIGHT_SNORE:        return 1;
        case HLTH_RISK_BREATH_RESTRICTION: return 2;
        case HLTH_RISK_HYPOPNEA_SUSPECTED: return 2;
        case HLTH_RISK_APNEA_SUSPECTED:    return 3;
        case HLTH_RISK_DATA_LOW_QUALITY:   return 4;
        default:                           return 4;
    }
}

static int fusion_event_to_id(fusion_event_t e)
{
    switch (e) {
        case FUSION_NORMAL:                return 0;
        case FUSION_SIMPLE_SNORE:          return 1;
        case FUSION_SUSPECTED_HYPOPNEA:    return 2;
        case FUSION_SUSPECTED_OBSTRUCTIVE: return 3;
        case FUSION_SUSPECTED_CENTRAL:     return 3;
        case FUSION_MOVEMENT_ARTIFACT:     return 4;
        case FUSION_BODY_MOVEMENT_AROUSAL: return 4;
        case FUSION_DATA_QUALITY_LOW:      return 5;
        case FUSION_WARMING_UP:            return 5;
        default:                           return 5;
    }
}

bool main_report_uart_init(void)
{
    s_send_count = 0;
    s_last_stat_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

    /* UART2 由 snore_report_rx_start() 统一安装。这里不重复安装，避免 UART 状态混乱。 */
    ESP_LOGI(TAG, "BOARD_LINK ready UART=%d RX=%d TX=%d baud=%d",
             BOARD_LINK_UART, BOARD_LINK_RX_GPIO, BOARD_LINK_TX_GPIO, BOARD_LINK_BAUD);
    ESP_LOGI(TAG, "MAIN_TX: ready");
    return true;
}

void main_report_uart_send(void)
{
    char buf[MAIN_TX_BUF_SIZE];
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

    snore_report_t snore;
    bool audio_valid = snore_report_rx_get_latest(&snore) && snore.valid && snore_report_is_fresh(3000);

    sleep_radar_data_t radar_snapshot;
    bool has_radar_snapshot = sleep_radar_data_get_snapshot(&radar_snapshot);
    int radar_status = has_radar_snapshot ? get_radar_status(&radar_snapshot) : 0;

    int heart_valid = 0, heart_bpm = 0;
    int breath_valid = 0, breath_bpm = 0;
    int motion_valid = 0, body_motion = 0;
    int bed_valid = 0, in_bed = 0;
    int stage_valid = 0, sleep_stage = 0;

    if (has_radar_snapshot && radar_status >= 1) {
        uint32_t now_tick_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        heart_bpm = radar_snapshot.heart_rate;
        heart_valid = (radar_snapshot.heart_rate > 0 &&
                       (uint32_t)(now_tick_ms - radar_snapshot.last_heart_rate_ms) <= 5000U) ? 1 : 0;
        breath_bpm = radar_snapshot.breath_rate;
        breath_valid = (radar_snapshot.breath_rate > 0 &&
                        (uint32_t)(now_tick_ms - radar_snapshot.last_breath_rate_ms) <= 5000U) ? 1 : 0;
        motion_valid = (radar_snapshot.last_body_motion_ms != 0 &&
                        (uint32_t)(now_tick_ms - radar_snapshot.last_body_motion_ms) <= 5000U) ? 1 : 0;
        body_motion = radar_snapshot.body_motion;
        bed_valid = (radar_snapshot.last_inbed_update_ms != 0 &&
                     (uint32_t)(now_tick_ms - radar_snapshot.last_inbed_update_ms) <= 10000U) ? 1 : 0;
        in_bed = (radar_snapshot.in_bed == 1) ? 1 : 0;
        stage_valid = 0;
        sleep_stage = 0;
    }

    fusion_result_t fr;
    memset(&fr, 0, sizeof(fr));
    int event_id = 5;
    uint16_t apnea_cnt = 0, hypopnea_cnt = 0;
    if (sleep_fusion_get_result(&fr) && fr.data_valid) {
        event_id = fusion_event_to_id(fr.event);
    }
    sleep_fusion_get_stats(&apnea_cnt, &hypopnea_cnt, NULL, NULL);

    int risk_valid = 0, risk_level = 4;
    const health_fusion_result_t *hf = sleep_health_fusion_get();
    if (hf && (audio_valid || radar_status == 2) && hf->confidence > 0) {
        risk_valid = 1;
        risk_level = health_risk_to_level(hf->health_risk);
    }

    int system_status = get_system_status(audio_valid, radar_status);

    int wifi_ok = 1;
    int sd_ok = 0;
    int spo2_valid = 0, spo2 = 0;
    int temp_valid = 0, temp_x10 = 0, hum_x10 = 0;

    int len = snprintf(buf, sizeof(buf),
        "MAIN,%lu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%u,%u,%d,%d,%d,%d,%d,%d,%d,%d\n",
        (unsigned long)now_ms,
        radar_status,
        heart_valid, heart_bpm,
        breath_valid, breath_bpm,
        motion_valid, body_motion,
        bed_valid, in_bed,
        stage_valid, sleep_stage,
        risk_valid, risk_level,
        event_id,
        (unsigned)apnea_cnt, (unsigned)hypopnea_cnt,
        system_status,
        wifi_ok, sd_ok,
        spo2_valid, spo2,
        temp_valid, temp_x10, hum_x10);

    if (len > 0 && len < (int)sizeof(buf)) {
        int sent = uart_write_bytes(BOARD_LINK_UART, buf, (size_t)len);
        if (sent != len) {
            ESP_LOGW(TAG, "uart_write_bytes sent=%d expect=%d", sent, len);
        }
    } else {
        ESP_LOGW(TAG, "MAIN line truncated len=%d", len);
    }

    ++s_send_count;
    if ((uint32_t)(now_ms - s_last_stat_ms) >= 5000U) {
        ESP_LOGI(TAG, "MAIN_TX count=%lu radar_status=%d heart_valid=%d heart=%d breath_valid=%d breath=%d risk_valid=%d risk=%d",
                 (unsigned long)s_send_count, radar_status, heart_valid, heart_bpm,
                 breath_valid, breath_bpm, risk_valid, risk_level);
        s_last_stat_ms = now_ms;
    }
}
