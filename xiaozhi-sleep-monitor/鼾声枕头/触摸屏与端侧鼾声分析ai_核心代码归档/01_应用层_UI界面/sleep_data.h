#ifndef SLEEP_DATA_H
#define SLEEP_DATA_H

#include <stdint.h>
#include <stdbool.h>

/* ===================== 页面枚举 ===================== */
typedef enum
{
    SLEEP_PAGE_HOME = 0,
    SLEEP_PAGE_SETTING,
    SLEEP_PAGE_MAX
} SleepPage_t;

/* ===================== 睡姿 ===================== */
typedef enum
{
    POSTURE_SUPINE = 0,
    POSTURE_LEFT,
    POSTURE_RIGHT,
    POSTURE_PRONE
} SleepPosture_t;

/* ===================== 风险等级 ===================== */
typedef enum
{
    RISK_NORMAL = 0,
    RISK_LOW,
    RISK_MIDDLE,
    RISK_HIGH
} SleepRisk_t;

/* ===================== 睡眠阶段 ===================== */
typedef enum
{
    SLEEP_STAGE_AWAKE = 0,
    SLEEP_STAGE_LIGHT,
    SLEEP_STAGE_DEEP,
    SLEEP_STAGE_AWAY
} SleepStage_t;

/* ===================== 系统状态 ===================== */
typedef enum
{
    SYS_STATE_STANDBY = 0,
    SYS_STATE_AUDIO_ONLY,    /* 音频有效但雷达未启用 */
    SYS_STATE_MONITORING,
    SYS_STATE_SLEEPING,
    SYS_STATE_REPORT_READY
} SystemState_t;

/* ===================== 环境舒适度 ===================== */
typedef enum
{
    COMFORT_GOOD = 0,
    COMFORT_TOO_HOT,
    COMFORT_TOO_COLD,
    COMFORT_TOO_DRY,
    COMFORT_TOO_HUMID
} ComfortLevel_t;

/* ===================== 传感器状态 ===================== */
typedef struct
{
    bool radar_online;
    bool mic_online;
    bool sd_online;
    bool wifi_connected;
    uint32_t last_radar_update_ms;
    uint32_t last_mic_update_ms;
    uint32_t last_env_update_ms;
} SensorStatus_t;

/* ===================== 睡眠数据结构 ===================== */
typedef struct
{
    /* 生命体征 */
    int spo2;
    int heart_rate;
    int snore_db;           /* 鼾声分贝 */
    int breath_rate;
    int sleep_score;
    int movement_level;     /* 体动强度 0-100 */
    bool in_bed;            /* 在床/离床 */

    /* 统计计数 */
    int apnea_count;
    int snore_count;
    int turn_over_count;
    int min_spo2;
    int max_snore_db;

    /* BLE手表/血氧来源状态 */
    bool watch_online;
    bool watch_spo2_valid;
    int  watch_spo2;
    bool watch_spo2_is_reference;
    bool watch_hr_valid;
    int  watch_hr;
    int  watch_step;
    int  watch_posture;
    uint32_t last_watch_update_ms;
    bool spo2_from_watch;

    /* 鼾声事件统计（由 sleep_monitor_data_adapter 统一维护）
     * snore_count 与 snore_type_count[] 在同一次事件上升沿/事件闭合中更新，
     * 避免“总次数”和“类型次数”各算各的。
     */
    uint32_t current_snore_episode_ms;  /* 当前鼾声片段持续时间 */
    uint32_t snore_total_ms;            /* 鼾声累计时长 */
    uint32_t longest_snore_episode_ms;  /* 最长单次鼾声时长 */
    uint8_t  last_snore_event_type;     /* 最近一次计入统计的类型: 1~4, 5=unknown */

    /* 高级呼吸事件指标 */
    float ahi;                    /* 每小时呼吸事件数 */
    float t90_percent;            /* 血氧低于 90% 的时间占比 */
    int delta_hr;                 /* 事件前后心率变化 */
    int apnea_duration_sec;       /* 当前/最近事件持续时间 */
    int airflow_reduction_percent;/* 气流幅度下降比例 */
    int spo2_drop_percent;        /* 当前/最近事件血氧下降 */
    int supine_event_percent;     /* 仰卧事件占比 */
    bool apnea_active;            /* 当前是否处于疑似呼吸事件 */

    /* 鼾声分类 (来自 snore_audio_analyzer) */
    int snore_type;               /* 0=none 1=nasal鼻鼾 2=throat喉鼾 3=mouth口呼吸 4=mixed 5=unknown */
    int snore_type_confidence;    /* 置信度 0~100 */
    int spectral_centroid_hz;     /* 频谱质心 Hz */
    int low_freq_ratio_x100;      /* 低频能量比例 0~100 */
    int harmonic_ratio_x100;      /* 谐波比例 0~100 */
    int airflow_sound_present;    /* 声学气流线索,非医学诊断 */
    int recovery_breath_sound;    /* 声学恢复呼吸线索,非医学诊断 */

    /* 鼾声类型统计 */
    uint32_t snore_type_count[6]; /* 各类型出现次数 */
    uint32_t nasal_snore_ms;
    uint32_t throat_snore_ms;
    uint32_t mouth_snore_ms;
    uint32_t mixed_snore_ms;

    /* 主板 MAIN 报告额外字段 (v2 31字段) */
    int event_id;                 /* 当前事件ID */
    int event_confidence;         /* 事件置信度 */
    int breath_amp;               /* 呼吸幅度 */
    int baseline_ok;              /* 基线就绪 */
    int baseline_progress;        /* 基线进度 0-120 */
    int distance_cm;              /* 人体距离 cm */
    int hypopnea_count;           /* 低通气次数 */
    int snore_resp_score;         /* 呼吸专项评分 (来自主板融合) */
    int system_status;           /* 主板系统状态 */
    bool main_online;             /* 主板在线 */
    bool has_main_ext;            /* 是否收到v2扩展字段 */

    /* 环境 */
    float temperature;
    float humidity;
    ComfortLevel_t comfort;

    /* 状态 */
    SleepPosture_t posture;
    SleepRisk_t risk_level;
    SleepStage_t sleep_stage;
    SystemState_t system_state;
    uint32_t monitor_duration_sec;

    /* 传感器在线状态 */
    SensorStatus_t sensor;

    /* 数据更新时间戳（毫秒）用于超时检测 */
    uint32_t data_timestamp_ms;
} SleepData_t;

extern SleepData_t g_sleep_data;

void sleep_data_init(void);
void sleep_data_mock_update(void);
void sleep_data_update_from_watch(int temp, int humi, int hr, int spo2, int step, int posture);

/* 检查某项数据是否超时（超过 10 秒未更新） */
bool sleep_data_is_timeout(uint32_t last_update_ms);

#endif
