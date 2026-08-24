/**
 ******************************************************************************
 * @file    sleep_radar_data.c
 * @brief   统一雷达数据结构：mutex 保护、更新函数、快照
 ******************************************************************************
 */

#include "sleep_radar_data.h"
#include "string.h"
#include "esp_log.h"

static const char *TAG = "RADAR_DATA";

static sleep_radar_data_t g_radar_data;
static SemaphoreHandle_t  g_radar_mutex = NULL;

/* ================================================================
 * 初始化
 * ================================================================ */

void sleep_radar_data_init(void)
{
    memset(&g_radar_data, 0, sizeof(g_radar_data));
    if (g_radar_mutex == NULL) {
        g_radar_mutex = xSemaphoreCreateMutex();
    }
    ESP_LOGI(TAG, "Sleep radar data initialized");
}

/* ================================================================
 * 访问接口
 * ================================================================ */

sleep_radar_data_t *sleep_radar_data_get(void)
{
    return &g_radar_data;
}

void sleep_radar_data_lock(void)
{
    if (g_radar_mutex) xSemaphoreTake(g_radar_mutex, portMAX_DELAY);
}

void sleep_radar_data_unlock(void)
{
    if (g_radar_mutex) xSemaphoreGive(g_radar_mutex);
}

bool sleep_radar_data_get_snapshot(sleep_radar_data_t *out)
{
    if (out == NULL) return false;
    sleep_radar_data_lock();
    memcpy(out, &g_radar_data, sizeof(sleep_radar_data_t));
    sleep_radar_data_unlock();
    return true;
}

/* ================================================================
 * ctrl=0x80 人体存在
 * ================================================================ */

void sleep_radar_data_update_presence(uint8_t val, uint32_t now_ms)
{
    sleep_radar_data_lock();
    g_radar_data.presence = val;
    g_radar_data.last_presence_ms = now_ms;
    g_radar_data.last_update_ms = now_ms;
    sleep_radar_data_unlock();
}

void sleep_radar_data_update_motion_state(uint8_t val, uint32_t now_ms)
{
    sleep_radar_data_lock();
    g_radar_data.motion_state = val;
    g_radar_data.last_motion_ms = now_ms;
    g_radar_data.last_update_ms = now_ms;
    sleep_radar_data_unlock();
}

void sleep_radar_data_update_body_motion(uint8_t val, uint32_t now_ms)
{
    sleep_radar_data_lock();
    g_radar_data.body_motion = val;
    g_radar_data.last_body_motion_ms = now_ms;
    g_radar_data.last_update_ms = now_ms;
    sleep_radar_data_unlock();
}

void sleep_radar_data_update_distance(uint16_t val, uint32_t now_ms)
{
    sleep_radar_data_lock();
    g_radar_data.distance_cm = val;
    g_radar_data.last_distance_ms = now_ms;
    g_radar_data.last_update_ms = now_ms;
    sleep_radar_data_unlock();
}

void sleep_radar_data_update_target_3d(int16_t x, int16_t y, int16_t z, uint32_t now_ms)
{
    sleep_radar_data_lock();
    g_radar_data.target_x_cm = x;
    g_radar_data.target_y_cm = y;
    g_radar_data.target_z_cm = z;
    g_radar_data.last_update_ms = now_ms;
    sleep_radar_data_unlock();
}

/* ================================================================
 * ctrl=0x85 心率
 * ================================================================ */

void sleep_radar_data_update_heart_rate(uint8_t val, uint32_t now_ms)
{
    sleep_radar_data_lock();
    g_radar_data.heart_rate = val;
    g_radar_data.last_heart_rate_ms = now_ms;
    g_radar_data.last_update_ms = now_ms;
    sleep_radar_data_unlock();
}

void sleep_radar_data_update_heart_wave_buffer(const uint8_t *data, uint8_t len, uint32_t now_ms)
{
    if (data == NULL || len == 0) return;
    sleep_radar_data_lock();
    for (uint8_t i = 0; i < len; i++) {
        g_radar_data.heart_wave_buf[g_radar_data.heart_wave_idx] = data[i];
        g_radar_data.heart_wave_idx = (g_radar_data.heart_wave_idx + 1) % HEART_WAVE_BUF_SIZE;
        if (g_radar_data.heart_wave_count < HEART_WAVE_BUF_SIZE) {
            g_radar_data.heart_wave_count++;
        }
    }
    g_radar_data.heart_wave_latest = data[len - 1];
    g_radar_data.hw_packet_count++;
    g_radar_data.last_heart_wave_ms = now_ms;
    g_radar_data.last_update_ms = now_ms;
    sleep_radar_data_unlock();
}

/* ================================================================
 * ctrl=0x81 呼吸
 * ================================================================ */

void sleep_radar_data_update_breath_status(uint8_t val, uint32_t now_ms)
{
    sleep_radar_data_lock();
    g_radar_data.breath_status = val;
    g_radar_data.last_breath_status_ms = now_ms;
    g_radar_data.last_update_ms = now_ms;
    sleep_radar_data_unlock();
}

void sleep_radar_data_update_breath_rate(uint8_t val, uint32_t now_ms)
{
    sleep_radar_data_lock();
    g_radar_data.breath_rate = val;
    g_radar_data.last_breath_rate_ms = now_ms;
    g_radar_data.last_update_ms = now_ms;
    sleep_radar_data_unlock();
}

void sleep_radar_data_update_breath_wave_buffer(const uint8_t *data, uint8_t len, uint32_t now_ms)
{
    if (data == NULL || len == 0) return;
    sleep_radar_data_lock();
    for (uint8_t i = 0; i < len; i++) {
        g_radar_data.breath_wave_buf[g_radar_data.breath_wave_idx] = data[i];
        g_radar_data.breath_wave_idx = (g_radar_data.breath_wave_idx + 1) % BREATH_WAVE_BUF_SIZE;
        if (g_radar_data.breath_wave_count < BREATH_WAVE_BUF_SIZE) {
            g_radar_data.breath_wave_count++;
        }
    }
    g_radar_data.breath_wave_latest = data[len - 1];
    g_radar_data.bw_packet_count++;
    g_radar_data.last_breath_wave_ms = now_ms;
    g_radar_data.last_update_ms = now_ms;
    sleep_radar_data_unlock();
}

/* ================================================================
 * ctrl=0x84 睡眠
 * ================================================================ */

void sleep_radar_data_update_in_bed(uint8_t val, uint32_t now_ms)
{
    sleep_radar_data_lock();
    g_radar_data.in_bed = val;
    g_radar_data.last_inbed_update_ms = now_ms;
    g_radar_data.last_update_ms = now_ms;
    sleep_radar_data_unlock();
}

void sleep_radar_data_update_sleep_stage(uint8_t val, uint32_t now_ms)
{
    sleep_radar_data_lock();
    g_radar_data.sleep_stage = val;
    g_radar_data.last_sleep_stage_update_ms = now_ms;
    g_radar_data.last_update_ms = now_ms;
    sleep_radar_data_unlock();
}

void sleep_radar_data_update_awake_duration(uint16_t val, uint32_t now_ms)
{
    sleep_radar_data_lock();
    g_radar_data.awake_min = val;
    g_radar_data.last_update_ms = now_ms;
    sleep_radar_data_unlock();
}

void sleep_radar_data_update_light_sleep_duration(uint16_t val, uint32_t now_ms)
{
    sleep_radar_data_lock();
    g_radar_data.light_sleep_min = val;
    g_radar_data.last_update_ms = now_ms;
    sleep_radar_data_unlock();
}

void sleep_radar_data_update_deep_sleep_duration(uint16_t val, uint32_t now_ms)
{
    sleep_radar_data_lock();
    g_radar_data.deep_sleep_min = val;
    g_radar_data.last_update_ms = now_ms;
    sleep_radar_data_unlock();
}

void sleep_radar_data_update_sleep_score(uint8_t val, uint32_t now_ms)
{
    sleep_radar_data_lock();
    g_radar_data.radar_sleep_score = val;
    g_radar_data.last_sleep_score_ms = now_ms;
    g_radar_data.last_update_ms = now_ms;
    sleep_radar_data_unlock();
}

void sleep_radar_data_update_sleep_composite(const uint8_t *payload, uint8_t len, uint32_t now_ms)
{
    sleep_radar_data_lock();
    /* 手册 8 字节顺序:
     * [0] 大幅度体动占比  [1] 小幅度体动占比  [2] 呼吸暂停次数
     * [3] 睡眠状态(3离床2清醒1浅睡0深睡)  [4] 平均呼吸  [5] 平均心跳
     * [6] 翻身次数  [7] 存在状态(1有人0无人) */
    if (len >= 1) g_radar_data.large_motion_ratio    = payload[0];
    if (len >= 2) g_radar_data.small_motion_ratio    = payload[1];
    if (len >= 3) g_radar_data.apnea_count_10min     = payload[2];
    if (len >= 4) g_radar_data.combined_sleep_state  = payload[3];
    if (len >= 5) g_radar_data.avg_breath_rate_10min = payload[4];
    if (len >= 6) g_radar_data.avg_heart_rate_10min  = payload[5];
    if (len >= 7) g_radar_data.turn_over_count_10min = payload[6];
    if (len >= 8) g_radar_data.combined_presence     = payload[7];
    g_radar_data.last_update_ms = now_ms;
    sleep_radar_data_unlock();
}

void sleep_radar_data_update_sleep_quality(uint8_t val, uint32_t now_ms)
{
    sleep_radar_data_lock();
    g_radar_data.sleep_quality = val;
    g_radar_data.last_sleep_quality_ms = now_ms;
    g_radar_data.last_update_ms = now_ms;
    sleep_radar_data_unlock();
}

void sleep_radar_data_update_struggle(uint8_t val, uint32_t now_ms)
{
    sleep_radar_data_lock();
    g_radar_data.struggle_state = val;
    g_radar_data.last_struggle_ms = now_ms;
    g_radar_data.last_update_ms = now_ms;
    sleep_radar_data_unlock();
}

void sleep_radar_data_update_no_person_timer(uint16_t val, uint32_t now_ms)
{
    sleep_radar_data_lock();
    g_radar_data.no_person_timer = val;
    g_radar_data.last_no_person_ms = now_ms;
    g_radar_data.last_update_ms = now_ms;
    sleep_radar_data_unlock();
}

void sleep_radar_data_update_vendor_status(uint8_t val, uint32_t now_ms)
{
    sleep_radar_data_lock();
    g_radar_data.vendor_status_0707 = val;
    g_radar_data.last_update_ms = now_ms;
    sleep_radar_data_unlock();
}

void sleep_radar_data_update_heartbeat(uint8_t val, uint32_t now_ms)
{
    sleep_radar_data_lock();
    g_radar_data.heartbeat_counter = val;
    g_radar_data.radar_connected = true;
    g_radar_data.last_heartbeat_ms = now_ms;
    g_radar_data.last_update_ms = now_ms;
    sleep_radar_data_unlock();
}

/* ================================================================
 * 工具函数
 * ================================================================ */

const char *sleep_radar_stage_to_str(uint8_t stage)
{
    switch (stage) {
        case R60_SLEEP_DEEP:    return "深睡";
        case R60_SLEEP_LIGHT:   return "浅睡";
        case R60_SLEEP_AWAKE:   return "清醒";
        case R60_SLEEP_OUT_BED: return "离床";
        default:                  return "--";
    }
}

const char *sleep_radar_motion_state_to_str(uint8_t state)
{
    switch (state) {
        case MOTION_STATE_UNKNOWN: return "--";
        case MOTION_STATE_STILL:   return "静止";
        case MOTION_STATE_ACTIVE:  return "活跃";
        default:                   return "--";
    }
}

const char *sleep_radar_breath_status_to_str(uint8_t status)
{
    switch (status) {
        case BREATH_STATUS_NONE:     return "无";
        case BREATH_STATUS_NORMAL:   return "正常";
        case BREATH_STATUS_TOO_LOW:  return "过低";
        case BREATH_STATUS_TOO_HIGH: return "过高";
        default:                     return "--";
    }
}

bool sleep_radar_data_is_fresh(uint32_t field_update_ms, uint32_t threshold_ms)
{
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (field_update_ms == 0) return false;
    return (now - field_update_ms) <= threshold_ms;
}
