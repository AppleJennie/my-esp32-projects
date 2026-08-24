#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_heap_caps.h>

// 使用模拟音频源（不占用真实 I2S 麦克风，避免与主音频通道冲突）
#define SLEEP_MONITOR_SIMULATE_MIC 1

// ==========================================
// 硬件引脚配置 (ESP32-S3-DevKitC-1)
// ==========================================

// I2C 总线0 (生理传感器)
#define I2C_SDA_0       8
#define I2C_SCL_0       9
#define I2C_FREQ_0      400000

// I2C 总线1 (环境传感器 - 可选)
#define I2C_SDA_1       10
#define I2C_SCL_1       11
#define I2C_FREQ_1      100000

// I2S 麦克风 (INMP441)
#define I2S_WS          4
#define I2S_SD          5
#define I2S_SCK         6
#define I2S_PORT        I2S_NUM_0

// 毫米波雷达 UART
#define RADAR_UART_NUM  UART_NUM_1
#define RADAR_TX        17
#define RADAR_RX        18
#define RADAR_BAUD      115200

// LED 状态指示
#define LED_STATUS      2
#define LED_ALERT       3

// ==========================================
// 采样率与缓冲区配置
// ==========================================

// 音频: 16kHz, 16bit, 单声道
#define AUDIO_SAMPLE_RATE       16000
#define AUDIO_BITS              16
#define AUDIO_CHANNELS          1
#define AUDIO_BUFFER_MS         100
#define AUDIO_CHUNK_SAMPLES     (AUDIO_SAMPLE_RATE * AUDIO_BUFFER_MS / 1000)  // 1600 samples
#define AUDIO_CHUNK_BYTES       (AUDIO_CHUNK_SAMPLES * sizeof(int16_t))

// 雷达: 20Hz 胸腔位移
#define RADAR_SAMPLE_RATE       20
#define RADAR_FRAME_SIZE        8
#define RADAR_HISTORY_SEC       30
#define RADAR_HISTORY_SIZE      (RADAR_SAMPLE_RATE * RADAR_HISTORY_SEC)

// 生理信号: 100Hz (来自MAX30102)
#define PHYSIO_SAMPLE_RATE      100
#define PHYSIO_DECIMATED_RATE   25      // 降采样后用于特征提取
#define PHYSIO_FRAME_SIZE       4       // SpO2, HR, PPG, 状态
#define PHYSIO_HISTORY_SEC      60
#define PHYSIO_HISTORY_SIZE     (PHYSIO_DECIMATED_RATE * PHYSIO_HISTORY_SEC)

// 环境: 1Hz
#define ENV_SAMPLE_RATE         1

// ==========================================
// AI 模型配置
// ==========================================

// 音频特征 (MFCC)
#define AUDIO_MFCC_NUM            13
#define AUDIO_MFCC_BINS           40
#define AUDIO_MFCCFrames_PER_INF  49   // ~3秒音频
#define AUDIO_FEATURE_SIZE        (AUDIO_MFCC_NUM * AUDIO_MFCCFrames_PER_INF)

// 雷达特征
#define RADAR_FEATURE_SIZE      64

// 生理特征
#define PHYSIO_FEATURE_SIZE     32

// TFLM 张量分配 (单任务鼾声检测模型 < 20KB, 60KB arena 足够)
#define TFLM_TENSOR_ARENA_SIZE  (60 * 1024)  // 60KB in PSRAM

// ==========================================
// 事件与队列
// ==========================================
#define AUDIO_QUEUE_LENGTH      4
#define RADAR_QUEUE_LENGTH      8
#define PHYSIO_QUEUE_LENGTH     8
#define INFERENCE_QUEUE_LENGTH  4

// ==========================================
// 任务配置
// ==========================================
#define SENSOR_TASK_STACK       8192
#define SENSOR_TASK_PRIORITY    3
#define AUDIO_TASK_STACK        8192
#define AUDIO_TASK_PRIORITY     4
#define INFERENCE_TASK_STACK    16384
#define INFERENCE_TASK_PRIORITY 2

// ==========================================
// 睡眠分析参数
// ==========================================
#define APNEA_THRESHOLD_SPO2        90.0f   // 血氧低于90%视为危险
#define APNEA_THRESHOLD_RR_PAUSE    10.0f   // 呼吸暂停 > 10秒
#define APNEA_HYPOVENTILATION_RR    8.0f    // 低通气阈值
#define SNORE_DB_THRESHOLD          40.0f   // 鼾声分贝阈值
#define AHI_NORMAL                  5.0f    // AHI < 5 正常
#define AHI_MILD                    15.0f   // AHI < 15 轻度
#define AHI_MODERATE                30.0f   // AHI < 30 中度
#define AHI_SEVERE                  30.0f   // AHI >= 30 重度

// ==========================================
// 睡眠阶段定义
// ==========================================
typedef enum {
    STAGE_AWAKE = 0,
    STAGE_LIGHT,       // N1/N2
    STAGE_DEEP,        // N3
    STAGE_REM,
    STAGE_UNKNOWN
} SleepStage;

// ==========================================
// 风险等级
// ==========================================
typedef enum {
    RISK_LOW = 0,
    RISK_MODERATE,
    RISK_HIGH,
    RISK_CRITICAL
} ApneaRisk;

// ==========================================
// 数据帧结构
// ==========================================

typedef struct {
    int16_t samples[AUDIO_CHUNK_SAMPLES];
    uint32_t timestamp_ms;
    float rms_db;
} AudioChunk;

typedef struct {
    float radar_points[3][RADAR_FRAME_SIZE];  // x,y,z 胸腔位移
    float vital_signs[2];                     // 呼吸率, 心率
    uint32_t timestamp_ms;
} RadarFrame;

typedef struct {
    float spo2;
    float hr;
    float ppg;
    int status;
    uint32_t timestamp_ms;
} PhysioFrame;

typedef struct {
    float temp;
    float humidity;
    float light;
    uint32_t timestamp_ms;
} EnvFrame;

// ==========================================
// 推理输入输出结构
// ==========================================

typedef struct {
    float audio_features[AUDIO_FEATURE_SIZE];  // MFCC: 13x49
    float radar_features[RADAR_FEATURE_SIZE];  // FFT stats
    float physio_features[PHYSIO_FEATURE_SIZE];// SpO2/HR stats
    uint32_t timestamp_ms;
} InferenceInput;

typedef struct {
    float snore_probability;   // 鼾声概率
    float apnea_probability;   // 呼吸暂停概率
    float stage_probability[5];// 睡眠阶段概率
    ApneaRisk risk_level;
    float ahi_estimate;        // AHI 估算
} InferenceOutput;

// ==========================================
// 睡眠报告结构
// ==========================================

typedef struct {
    uint32_t total_sleep_sec;
    uint32_t deep_sleep_sec;
    uint32_t light_sleep_sec;
    uint32_t rem_sleep_sec;
    uint32_t awake_count;
    uint32_t snore_count;
    uint32_t apnea_event_count;
    float avg_spo2;
    float min_spo2;
    float avg_hr;
    float max_hr;
    float ahi;
    ApneaRisk overall_risk;
} SleepReport;

// ==========================================
// 系统事件标志
// ==========================================

#define EVENT_SENSOR_READY      (1 << 0)
#define EVENT_AUDIO_READY       (1 << 1)
#define EVENT_INFERENCE_DONE    (1 << 2)
#define EVENT_ALERT_TRIGGERED   (1 << 3)
#define EVENT_REPORT_READY      (1 << 4)

// ==========================================
// 日志标签
// ==========================================

#define TAG_APP         "SLEEP_APP"
#define TAG_SENSOR      "SENSOR"
#define TAG_AUDIO       "AUDIO"
#define TAG_AI          "AI_MODEL"
#define TAG_REPORT      "REPORT"

// ==========================================
// 内存分配宏 (优先 PSRAM)
// ==========================================

#define TFLM_ALLOC(size)  heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define TFLM_FREE(ptr)    free(ptr)

// ==========================================
// 调试开关
// ==========================================

// #define DEBUG_AUDIO_DUMP        1   // 打印音频 RMS
// #define DEBUG_INFERENCE_TIME    1   // 打印推理耗时
