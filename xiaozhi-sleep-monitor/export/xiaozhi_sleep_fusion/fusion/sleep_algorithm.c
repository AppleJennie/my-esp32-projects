/**
 ******************************************************************************
 * @file    sleep_algorithm.c
 * @brief   自研睡眠算法 —— 基于雷达原始数据推断，不依赖雷达自带报告
 ******************************************************************************
 */

#include "sleep_algorithm.h"
#include "esp_log.h"

static const char *TAG = "SLEEP_ALGO";

#define INFER_FRESH_MS  5000   /* 5 秒内数据视为有效 */

void sleep_algorithm_init(void)
{
    ESP_LOGI(TAG, "Sleep algorithm initialized (inference mode)");
}

void sleep_algorithm_calc(const sleep_radar_data_t *r, sleep_calc_t *out)
{
    if (!r || !out) return;

    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    memset(out, 0, sizeof(sleep_calc_t));

    /* ---- 推断有人 ---- */
    bool has_data = false;
    if ((now - r->last_distance_ms) <= INFER_FRESH_MS && r->distance_cm > 0 && r->distance_cm < 300)
        has_data = true;
    if ((now - r->last_heart_rate_ms) <= INFER_FRESH_MS && r->heart_rate > 0)
        has_data = true;
    if ((now - r->last_breath_rate_ms) <= INFER_FRESH_MS && r->breath_rate > 0)
        has_data = true;
    if ((now - r->last_body_motion_ms) <= INFER_FRESH_MS && r->body_motion > 0)
        has_data = true;

    out->inferred_presence = has_data;

    /* presence_source: 优先雷达正式上报 */
    if ((now - r->last_presence_ms) <= INFER_FRESH_MS) {
        out->presence_source = PRESENCE_SRC_RADAR;
    } else if (has_data) {
        out->presence_source = PRESENCE_SRC_INFERRED;
    } else {
        out->presence_source = PRESENCE_SRC_UNKNOWN;
    }

    /* ---- 推断呼吸状态 ---- */
    if ((now - r->last_breath_rate_ms) <= INFER_FRESH_MS && r->breath_rate > 0) {
        if (r->breath_rate < 10)       out->inferred_breath_status = BREATH_STATUS_TOO_LOW;
        else if (r->breath_rate <= 25) out->inferred_breath_status = BREATH_STATUS_NORMAL;
        else                           out->inferred_breath_status = BREATH_STATUS_TOO_HIGH;
    } else {
        out->inferred_breath_status = BREATH_STATUS_UNKNOWN;
    }

    /* ---- 体动等级 ---- */
    if ((now - r->last_body_motion_ms) <= INFER_FRESH_MS) {
        if (r->body_motion <= 5)       out->body_motion_level = MOTION_LOW;
        else if (r->body_motion <= 30) out->body_motion_level = MOTION_MEDIUM;
        else                           out->body_motion_level = MOTION_HIGH;
    }

    /* ---- 简单风险分（0~100，仅供 demo） ---- */
    uint8_t risk = 0;
    if (out->inferred_breath_status == BREATH_STATUS_TOO_LOW)  risk += 30;
    if (out->inferred_breath_status == BREATH_STATUS_TOO_HIGH) risk += 20;
    if (out->body_motion_level == MOTION_HIGH)                 risk += 20;
    if (!has_data)                                             risk += 50; /* 无数据 */
    if (risk > 100) risk = 100;
    out->simple_risk_score = risk;
}
