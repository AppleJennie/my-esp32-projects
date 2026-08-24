/**
 * Wi-Fi 管理：STA 模式连接（ESP32-C5 双频自动扫描，5GHz 优先交给 AP 协商）
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 连接状态变化回调，true = 已拿到 IP */
typedef void (*wifi_mgr_cb_t)(bool connected);

/**
 * 启动 Wi-Fi。SSID 在 menuconfig -> Moji2 配置 中设置；
 * SSID 为空时直接返回，不启用 Wi-Fi。
 */
esp_err_t wifi_mgr_start(wifi_mgr_cb_t cb);

bool wifi_mgr_is_connected(void);

#ifdef __cplusplus
}
#endif
