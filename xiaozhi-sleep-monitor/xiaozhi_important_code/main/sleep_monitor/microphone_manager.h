#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化麦克风（封装 I2S 驱动，带冲突检测）
 * @return true 成功
 */
bool microphone_manager_init(void);

/**
 * @brief 反初始化麦克风，释放 I2S 端口
 */
void microphone_manager_deinit(void);

/**
 * @brief 是否已初始化
 */
bool microphone_manager_is_initialized(void);

#ifdef __cplusplus
}
#endif
