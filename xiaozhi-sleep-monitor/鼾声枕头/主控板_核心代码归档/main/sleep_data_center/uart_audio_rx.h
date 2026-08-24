#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动混合 UART 接收（921600 8N1）+ 音频 AI 推理链路
 *        接收任务负责读取串口数据并分发给 JSON 解析器和音频队列。
 *        AI 推理在独立任务 audio_ai_task 中执行，不阻塞 UART。
 * @return true 成功
 */
bool uart_audio_rx_start(void);

#ifdef __cplusplus
}
#endif
