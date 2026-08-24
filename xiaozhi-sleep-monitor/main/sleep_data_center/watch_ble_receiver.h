/**
 * watch_ble_receiver.h — OV-Watch BLE SPP 数据接收 (ESP32 NimBLE Central)
 *
 * 通信链路:
 *   OV-Watch (STM32) → KT6328A/KT6368A BLE SPP 模组 → ESP32-S3 NimBLE Central
 *
 * 协议: 文本命令 + JSON 返回 (一问一答)
 *   发送 "OV+SEND\r\n" → 返回 JSON {date, time, humi, temp, hr, spo2, step, posture}
 *   轮询间隔: 500ms
 */
#ifndef WATCH_BLE_RECEIVER_H
#define WATCH_BLE_RECEIVER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════
 * 配置 (可在 app_role_config.h 覆盖)
 * ═══════════════════════════════════════════════════════════════ */

/* 手表广播名前缀 (匹配即连接) */
#ifndef WATCH_BLE_NAME_PREFIX
#define WATCH_BLE_NAME_PREFIX       "KT6368A-BL"
#endif

/* BLE SPP 服务 UUID (KT6368A 原厂 0xFFF0) */
#ifndef WATCH_BLE_SVC_UUID
#define WATCH_BLE_SVC_UUID          0xFFF0
#endif

/* 写特征 UUID (手机→模块, 发命令) */
#ifndef WATCH_BLE_WRITE_UUID
#define WATCH_BLE_WRITE_UUID        0xFFF1
#endif

/* Notify 特征 UUID (模块→手机, 收 JSON) */
#ifndef WATCH_BLE_NOTIFY_UUID
#define WATCH_BLE_NOTIFY_UUID       0xFFF1
#endif

/* 轮询间隔 ms */
#ifndef WATCH_POLL_INTERVAL_MS
#define WATCH_POLL_INTERVAL_MS      500
#endif

/* 连接超时 ms */
#ifndef WATCH_CONNECT_TIMEOUT_MS
#define WATCH_CONNECT_TIMEOUT_MS    10000
#endif

/* 重连间隔 ms */
#ifndef WATCH_RECONNECT_INTERVAL_MS
#define WATCH_RECONNECT_INTERVAL_MS 5000
#endif

/* Notify 接收缓冲大小 */
#ifndef WATCH_RX_BUF_SIZE
#define WATCH_RX_BUF_SIZE           512
#endif

/* ═══════════════════════════════════════════════════════════════
 * 解析后数据
 * ═══════════════════════════════════════════════════════════════ */

typedef struct {
    bool    valid;
    uint8_t hr;           /* 心率 bpm */
    uint8_t spo2;         /* 血氧 % (含参考值标记) */
    bool    spo2_is_ref;  /* true=参考估算值, false=实测 */
    int16_t temp_x10;     /* 温度 ×10 (例如 265 = 26.5°C) */
    int16_t humi_x10;     /* 湿度 ×10 (例如 670 = 67.0%) */
    uint16_t step;        /* 步数 */
    uint8_t posture;      /* 0未知 1仰睡 2侧睡 3俯睡 */
    bool    battery_valid;
    uint8_t battery;      /* 电量 0~100 */
    uint32_t rx_timestamp;/* 接收时间戳 (ms) */
} watch_ble_data_t;

/* ═══════════════════════════════════════════════════════════════
 * API
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief 初始化 BLE 主机并开始扫描手表
 *        不会阻塞，后台自动扫描 → 连接 → 轮询
 * @return 0=成功, -1=失败
 */
int watch_ble_receiver_init(void);

/**
 * @brief 停止 BLE 接收（断开连接、停止扫描）
 */
void watch_ble_receiver_deinit(void);

/**
 * @brief 获取最新一次解析成功的手表数据
 * @return true=有有效数据
 */
bool watch_ble_get_latest(watch_ble_data_t *out);

/**
 * @brief 查询连接状态
 */
bool watch_ble_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif
