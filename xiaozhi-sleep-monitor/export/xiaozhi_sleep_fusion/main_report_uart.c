/**
 * main_report_uart.c — 小智主板 MAIN 数据发送实现
 *
 * 每秒从融合引擎读取数据 → 格式化 MAIN 文本 → uart_write_bytes 发给 UI 板
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

/* UART2 发送缓冲 */
#define MAIN_TX_BUF_SIZE  256

static uint32_t s_send_count = 0;
static uint32_t s_last_stat_ms = 0;

/* ── 雷达状态判定 ── */
static int get_radar_status(const sleep_radar_data_t *r)
{
    if (!r || !r->radar_connected) return 0;  /* OFF */

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    /* 5 秒内有更新 → valid, 否则 timeout */
    if ((now - r->last_update_ms) < 5000) {
        /* 有有效体征数据视为 stable */
        if (r->heart_rate > 0 || r->breath_rate > 0) return 2; /* valid */
        return 1; /* warming_up */
    }
    return 3; /* timeout/error */
}

/* ── 系统状态 ── */
static int get_system_status(bool audio_valid, int radar_status)
{
    if (!audio_valid && radar_status <= 1) return 3; /* 数据质量低 */
    if (audio_valid && radar_status == 0) return 0;   /* 仅音频 */
    if (audio_valid && radar_status == 1) return 1;   /* 等待主板 */
    if (audio_valid && radar_status >= 2) return 2;   /* 融合中 */
    return 3;
}

/* ── 健康风险转换 ── */
static int health_risk_to_level(health_risk_t r)
{
    switch (r) {
        case HLTH_RISK_NORMAL:                 return 0;
        case HLTH_RISK_LIGHT_SNORE:            return 1;
        case HLTH_RISK_BREATH_RESTRICTION:      return 2;
        case HLTH_RISK_HYPOPNEA_SUSPECTED:     return 2;
        case HLTH_RISK_APNEA_SUSPECTED:        return 3;
        case HLTH_RISK_DATA_LOW_QUALITY:       return 4;
        default:                               return 4;
    }
}

/* ── 融合事件转换 ── */
static int fusion_event_to_id(fusion_event_t e)
{
    switch (e) {
        case FUSION_NORMAL:               return 0;
        case FUSION_SIMPLE_SNORE:         return 1;
        case FUSION_SUSPECTED_HYPOPNEA:   return 2;
        case FUSION_SUSPECTED_OBSTRUCTIVE:return 3;
        case FUSION_SUSPECTED_CENTRAL:    return 3; /* 都归为暂停样 */
        case FUSION_MOVEMENT_ARTIFACT:    return 4;
        case FUSION_BODY_MOVEMENT_AROUSAL:return 4;
        case FUSION_DATA_QUALITY_LOW:     return 5;
        case FUSION_WARMING_UP:           return 5;
        default:                          return 5;
    }
}

/* ── 公开接口 ── */

bool main_report_uart_init(void)
{
    s_send_count = 0;
    s_last_stat_ms = (uint32_t)(esp_timer_get_time() / 1000);

    ESP_LOGI(TAG, "BOARD_LINK: UART2 init OK RX=%d TX=%d baud=%d",
             BOARD_LINK_RX_GPIO, BOARD_LINK_TX_GPIO, BOARD_LINK_BAUD);
    ESP_LOGI(TAG, "MAIN_TX: ready");
    return true;
}

void main_report_uart_send(void)
{
    char buf[MAIN_TX_BUF_SIZE];
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

    /* ── 获取各数据源 ── */
    sleep_radar_data_t *radar = sleep_radar_data_get();
    int radar_status = get_radar_status(radar);

    /* 雷达字段 */
    int heart_valid = 0, heart_bpm = 0;
    int breath_valid = 0, breath_bpm = 0;
    int motion_valid = 0, body_motion = 0;
    int bed_valid = 0, in_bed = 0;
    int stage_valid = 0, sleep_stage = 0;

    if (radar && radar->radar_connected && radar_status >= 1) {
        heart_bpm = radar->heart_rate;
        heart_valid = (radar->heart_rate > 0) ? 1 : 0;
        breath_bpm = radar->breath_rate;
        breath_valid = (radar->breath_rate > 0) ? 1 : 0;
        motion_valid = 1;
        body_motion = radar->body_motion;
        bed_valid = 1;
        in_bed = (radar->in_bed == 1) ? 1 : 0;
        stage_valid = 0; /* TODO: sleep_stage from radar */
        sleep_stage = 0;
    }

    /* 健康风险 */
    int risk_valid = 0, risk_level = 4;
    const health_fusion_result_t *hf = sleep_health_fusion_get();
    if (hf) {
        risk_valid = 1;
        risk_level = health_risk_to_level(hf->health_risk);
    }

    /* 融合事件 */
    int event_id = 5;
    uint16_t apnea_cnt = 0, hypopnea_cnt = 0;
    fusion_result_t fr;
    if (sleep_fusion_get_result(&fr) && fr.data_valid) {
        event_id = fusion_event_to_id(fr.event);
    }
    sleep_fusion_get_stats(&apnea_cnt, &hypopnea_cnt, NULL, NULL);

    /* SNORE 数据有效？ */
    snore_report_t snore;
    bool audio_valid = snore_report_rx_get_latest(&snore) && snore.valid && snore_report_is_fresh(3000);

    /* 系统状态 */
    int system_status = get_system_status(audio_valid, radar_status);

    /* WiFi / SD (简化：WiFi 默认 1, SD 默认 0) */
    int wifi_ok = 1;
    int sd_ok = 0; /* TODO: 接入 SD logger 状态 */

    /* 血氧 / 温湿度（暂未接入） */
    int spo2_valid = 0, spo2 = 0;
    int temp_valid = 0, temp_x10 = 0, hum_x10 = 0;

    /* ── 格式化 MAIN ── */
    int len = snprintf(buf, MAIN_TX_BUF_SIZE,
        "MAIN,%lu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
        (unsigned long)now_ms,
        radar_status,
        heart_valid, heart_bpm,
        breath_valid, breath_bpm,
        motion_valid, body_motion,
        bed_valid, in_bed,
        stage_valid, sleep_stage,
        risk_valid, risk_level,
        event_id,
        apnea_cnt, hypopnea_cnt,
        system_status,
        wifi_ok, sd_ok,
        spo2_valid, spo2,
        temp_valid, temp_x10, hum_x10);

    if (len > 0 && len < MAIN_TX_BUF_SIZE) {
        int sent = uart_write_bytes(BOARD_LINK_UART, buf, len);
        if (sent < 0) {
            ESP_LOGW(TAG, "uart_write_bytes failed: %d", sent);
        }
    }

    s_send_count++;

    /* 每 5 秒统计 */
    if (now_ms - s_last_stat_ms >= 5000) {
        ESP_LOGI(TAG, "MAIN_TX send OK radar_status=%d heart_valid=%d heart=%d "
                 "breath_valid=%d breath=%d risk=%d",
                 radar_status, heart_valid, heart_bpm,
                 breath_valid, breath_bpm, risk_level);
        s_last_stat_ms = now_ms;
    }
}
