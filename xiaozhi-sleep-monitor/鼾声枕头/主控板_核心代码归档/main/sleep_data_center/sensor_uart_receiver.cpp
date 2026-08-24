#include "sensor_uart_receiver.h"
#include "uart_audio_rx.h"
#include <esp_log.h>

static const char* TAG = "UART_RX";

bool sensor_uart_receiver_start(void) {
    ESP_LOGI(TAG, "启动混合 UART 接收链路 (921600 8N1 + AI 推理)");
    return uart_audio_rx_start();
}
