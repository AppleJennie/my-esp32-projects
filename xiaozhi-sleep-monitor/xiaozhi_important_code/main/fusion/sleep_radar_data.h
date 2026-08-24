#ifndef SLEEP_RADAR_DATA_H
#define SLEEP_RADAR_DATA_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 波形缓冲区大小
 * ================================================================ */
/* 波形缓冲区: 5Hz × 30秒 = 150点, 够用且省SRAM */
#define BREATH_WAVE_BUF_SIZE    150
#define HEART_WAVE_BUF_SIZE     150

/* 推断来源 */
typedef enum {
    PRESENCE_SRC_UNKNOWN  = 0,
    PRESENCE_SRC_RADAR    = 1,
    PRESENCE_SRC_INFERRED = 2,
} presence_source_t;

/* ================================================================
 * 枚举类型（与 r60abd1_uart.h 对齐）
 * ================================================================ */

/* 睡眠分期 (R60 内部枚举, 加 R60_ 前缀避免与 sleep_data.h 的 SleepStage_t 冲突) */
typedef enum {
    R60_SLEEP_DEEP    = 0,
    R60_SLEEP_LIGHT   = 1,
    R60_SLEEP_AWAKE   = 2,
    R60_SLEEP_OUT_BED = 3,
} sleep_stage_t;

/* 运动状态 */
typedef enum {
    MOTION_STATE_UNKNOWN = 0,
    MOTION_STATE_STILL   = 1,
    MOTION_STATE_ACTIVE  = 2,
} motion_state_t;

/* 呼吸状态 */
typedef enum {
    BREATH_STATUS_NONE     = 0,
    BREATH_STATUS_NORMAL   = 1,
    BREATH_STATUS_TOO_LOW  = 2,
    BREATH_STATUS_TOO_HIGH = 3,
    BREATH_STATUS_UNKNOWN  = 0xFF,
} breath_status_t;

/* ================================================================
 * 统一雷达数据结构体
 * ================================================================ */

typedef struct {
    /* ---- ctrl=0x80 人体存在 ---- */
    uint8_t  presence;             /* 0无人 1有人 */
    uint8_t  motion_state;         /* motion_state_t: 0未知 1静止 2活跃 */
    uint8_t  body_motion;          /* 体动幅度 0~100 */
    uint16_t distance_cm;          /* 人体距离 cm */
    int16_t  target_x_cm;
    int16_t  target_y_cm;
    int16_t  target_z_cm;

    /* ---- ctrl=0x85 心率 ---- */
    uint8_t  heart_rate;           /* 心率 bpm */
    uint8_t  heart_wave_latest;                     /* 最新心率波形值 */
    uint8_t  heart_wave_buf[HEART_WAVE_BUF_SIZE];  /* 心率波形 ring buffer */
    uint16_t heart_wave_idx;
    uint16_t heart_wave_count;

    /* ---- ctrl=0x81 呼吸 ---- */
    uint8_t  breath_status;        /* breath_status_t */
    uint8_t  breath_rate;          /* 呼吸率 次/分 */
    uint8_t  breath_wave_latest;                     /* 最新呼吸波形值（兼容旧代码） */
    uint8_t  breath_wave_buf[BREATH_WAVE_BUF_SIZE]; /* 呼吸波形 ring buffer */
    uint16_t breath_wave_idx;
    uint16_t breath_wave_count;

    /* ---- ctrl=0x84 睡眠 ---- */
    uint8_t  in_bed;               /* 0离床 1在床 */
    uint8_t  sleep_stage;          /* sleep_stage_t */
    uint16_t awake_min;            /* 清醒时长（分钟） */
    uint16_t light_sleep_min;      /* 浅睡时长（分钟） */
    uint16_t deep_sleep_min;       /* 深睡时长（分钟） */
    uint8_t  radar_sleep_score;    /* 睡眠评分 0~100 */
    uint8_t  sleep_quality;        /* 0较差 1一般 2良好 */

    /* 睡眠综合状态 (0x84 0x0C) */
    uint8_t  combined_presence;
    uint8_t  combined_sleep_state;
    uint8_t  avg_heart_rate_10min;
    uint8_t  avg_breath_rate_10min;
    uint8_t  turn_over_count_10min;
    uint8_t  large_motion_ratio;
    uint8_t  small_motion_ratio;
    uint8_t  apnea_count_10min;    /* 呼吸暂停次数（协议标注"暂无"） */

    /* 睡眠异常 */
    uint8_t  sleep_abnormal;       /* 0无 1睡眠不足 2睡眠过长 3异常无人 */
    uint8_t  struggle_state;       /* 0无 1正常 2异常 */
    uint16_t no_person_timer;      /* 无人计时 */
    uint8_t  vendor_status_0707;   /* ctrl=0x07 厂商状态 */

    /* 系统状态 */
    uint8_t  heartbeat_counter;
    bool     radar_connected;
    uint32_t bw_packet_count;      /* 呼吸波形包计数 */
    uint32_t hw_packet_count;      /* 心率波形包计数 */

    /* ---- 时间戳 ---- */
    uint32_t last_update_ms;
    uint32_t last_presence_ms;
    uint32_t last_motion_ms;
    uint32_t last_distance_ms;
    uint32_t last_body_motion_ms;
    uint32_t last_breath_status_ms;
    uint32_t last_breath_rate_ms;
    uint32_t last_breath_wave_ms;
    uint32_t last_heart_rate_ms;
    uint32_t last_heart_wave_ms;
    uint32_t last_inbed_update_ms;
    uint32_t last_sleep_stage_update_ms;
    uint32_t last_sleep_score_ms;
    uint32_t last_sleep_quality_ms;
    uint32_t last_struggle_ms;
    uint32_t last_no_person_ms;
    uint32_t last_heartbeat_ms;
} sleep_radar_data_t;

/* ================================================================
 * 显示用数据结构
 * ================================================================ */
typedef struct {
    char     presence_str[24];
    char     in_bed_str[24];
    char     sys_state_str[24];    /* "预热中"/"监测中"/"离床"/"无人" */
    char     posture_str[24];      /* "疑似仰睡"/"疑似侧睡"/"翻身中"/"无法判断" */
    char     heart_str[16];
    char     breath_str[16];
    char     breath_status_str[16];
    char     motion_str[16];
    char     motion_state_str[16];
    char     distance_str[16];
    char     radar_state[32];
    char     update_str[32];
    /* 调试统计 */
    uint32_t bw_packet_count;      /* 呼吸波形包数 */
    uint32_t hw_packet_count;      /* 心率波形包数 */
    uint32_t last_bw_ms;           /* 最近波形时间 */
    uint32_t last_hw_ms;
    uint32_t frame_count;
    uint32_t error_count;
    uint32_t unknown_count;
    char     last_raw_hex[64];
    uint32_t sd_raw_saved;
    uint32_t sd_csv_saved;
    uint32_t sd_write_errors;
} sleep_display_legacy_t;

/* ================================================================
 * 初始化与访问接口
 * ================================================================ */
void sleep_radar_data_init(void);
sleep_radar_data_t *sleep_radar_data_get(void);
void sleep_radar_data_lock(void);
void sleep_radar_data_unlock(void);
bool sleep_radar_data_get_snapshot(sleep_radar_data_t *out);

/* ---- 更新函数（按 ctrl+cmd 分类） ---- */

/* ctrl=0x80 人体存在 */
void sleep_radar_data_update_presence(uint8_t val, uint32_t now_ms);
void sleep_radar_data_update_motion_state(uint8_t val, uint32_t now_ms);
void sleep_radar_data_update_body_motion(uint8_t val, uint32_t now_ms);
void sleep_radar_data_update_distance(uint16_t val, uint32_t now_ms);
void sleep_radar_data_update_target_3d(int16_t x, int16_t y, int16_t z, uint32_t now_ms);

/* ctrl=0x85 心率 */
void sleep_radar_data_update_heart_rate(uint8_t val, uint32_t now_ms);
void sleep_radar_data_update_heart_wave_buffer(const uint8_t *data, uint8_t len, uint32_t now_ms);

/* ctrl=0x81 呼吸 */
void sleep_radar_data_update_breath_status(uint8_t val, uint32_t now_ms);
void sleep_radar_data_update_breath_rate(uint8_t val, uint32_t now_ms);
void sleep_radar_data_update_breath_wave_buffer(const uint8_t *data, uint8_t len, uint32_t now_ms);

/* ctrl=0x84 睡眠 */
void sleep_radar_data_update_in_bed(uint8_t val, uint32_t now_ms);
void sleep_radar_data_update_sleep_stage(uint8_t val, uint32_t now_ms);
void sleep_radar_data_update_awake_duration(uint16_t val, uint32_t now_ms);
void sleep_radar_data_update_light_sleep_duration(uint16_t val, uint32_t now_ms);
void sleep_radar_data_update_deep_sleep_duration(uint16_t val, uint32_t now_ms);
void sleep_radar_data_update_sleep_score(uint8_t val, uint32_t now_ms);
void sleep_radar_data_update_sleep_composite(const uint8_t *payload, uint8_t len, uint32_t now_ms);
void sleep_radar_data_update_sleep_quality(uint8_t val, uint32_t now_ms);
void sleep_radar_data_update_struggle(uint8_t val, uint32_t now_ms);
void sleep_radar_data_update_no_person_timer(uint16_t val, uint32_t now_ms);
void sleep_radar_data_update_vendor_status(uint8_t val, uint32_t now_ms);
void sleep_radar_data_update_heartbeat(uint8_t val, uint32_t now_ms);

/* ---- 工具函数 ---- */
const char *sleep_radar_stage_to_str(uint8_t stage);
const char *sleep_radar_motion_state_to_str(uint8_t state);
const char *sleep_radar_breath_status_to_str(uint8_t status);
bool sleep_radar_data_is_fresh(uint32_t field_update_ms, uint32_t threshold_ms);

#ifdef __cplusplus
}
#endif

#endif /* SLEEP_RADAR_DATA_H */
