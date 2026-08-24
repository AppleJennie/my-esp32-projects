/**
 * snore_report_rx.c — UI 板 SNORE 文本接收实现
 *
 * 独立 UART 接收 → 按行解析 → 前缀匹配 "SNORE," → 解析 14 字段
 */
#include "snore_report_rx.h"

#include <stdio.h>
#include <string.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "SNORE_RX";

/* ── 接收缓冲 ── */
#define SNORE_LINE_BUF_SIZE  256
#define SNORE_TASK_STACK     4096
#define SNORE_TASK_PRIO      3

static snore_report_t s_latest;
static uint32_t s_last_update_ms = 0;

/* ── 解析一行 SNORE 文本 ── */
static bool parse_snore_line(const char *line, int len, snore_report_t *out)
{
    if (len < 10) return false;
    if (strncmp(line, "SNORE,", 6) != 0) return false;

    memset(out, 0, sizeof(*out));
    out->rx_ms = (uint32_t)(esp_timer_get_time() / 1000);

    /* SNORE,ts_ms,mic_ok,audio_valid,snore_active,snore_score,
     * snore_db,rms,peak,zcr_x100,current_episode_ms,snore_total_ms,
     * longest_episode_ms,snore_episode_count,quality */
    int n = sscanf(line,
        "SNORE,%lu,%hhu,%hhu,%hhu,%hhu,%hhu,%hu,%hu,%hhu,%lu,%lu,%lu,%hu,%hhu",
        &out->ts_ms,
        &out->mic_ok,
        &out->audio_valid,
        &out->snore_active,
        &out->snore_score,
        &out->snore_db,
        &out->rms,
        &out->peak,
        &out->zcr_x100,
        &out->current_episode_ms,
        &out->snore_total_ms,
        &out->longest_episode_ms,
        &out->snore_episode_count,
        &out->quality);

    if (n >= 14) {
        out->valid = true;
        return true;
    }

    ESP_LOGW(TAG, "SNORE parse: got %d/14 fields: %.*s", n, (len < 80 ? len : 80), line);
    out->valid = false;
    return false;
}

/* ── UART 接收任务 ── */
static void snore_rx_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Task started, UART%d RX=GPIO%d baud=%d",
             UART_SNORE_RX_NUM, UART_SNORE_RX_RX, UART_SNORE_RX_BAUD);

    char line_buf[SNORE_LINE_BUF_SIZE];
    int line_pos = 0;
    uint32_t report_count = 0;

    while (1) {
        uint8_t ch;
        int len = uart_read_bytes(UART_SNORE_RX_NUM, &ch, 1, pdMS_TO_TICKS(100));

        if (len != 1) {
            /* 超时，无数据 */
            continue;
        }

        if (ch == '\n' || ch == '\r') {
            if (line_pos > 0) {
                line_buf[line_pos] = '\0';

                snore_report_t report;
                if (parse_snore_line(line_buf, line_pos, &report)) {
                    s_latest = report;
                    s_last_update_ms = report.rx_ms;
                    report_count++;

                    /* 每 60 次打印一次统计 */
                    if (report_count % 60 == 0) {
                        ESP_LOGI(TAG, "SNORE reports: %lu, latest: mic=%d valid=%d snore=%d score=%d",
                                 report_count, report.mic_ok, report.audio_valid,
                                 report.snore_active, report.snore_score);
                    }
                }
                line_pos = 0;
            }
        } else if (line_pos < SNORE_LINE_BUF_SIZE - 1) {
            line_buf[line_pos++] = (char)ch;
        } else {
            /* 行过长，丢弃 */
            line_pos = 0;
        }
    }
}

/* ── 启动入口 ── */
bool snore_report_rx_start(void)
{
    /* 配置 UART */
    uart_config_t uart_cfg = {
        .baud_rate = UART_SNORE_RX_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(UART_SNORE_RX_NUM, &uart_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        return false;
    }

    err = uart_set_pin(UART_SNORE_RX_NUM,
                       UART_SNORE_RX_TX, UART_SNORE_RX_RX,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        return false;
    }

    err = uart_driver_install(UART_SNORE_RX_NUM, 2048, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return false;
    }

    /* 初始状态：无效 */
    memset(&s_latest, 0, sizeof(s_latest));
    s_latest.valid = false;

    /* 创建接收任务 */
    BaseType_t ret = xTaskCreate(snore_rx_task, "snore_rx",
                                  SNORE_TASK_STACK, NULL,
                                  SNORE_TASK_PRIO, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Task create failed");
        return false;
    }

    ESP_LOGI(TAG, "Init OK, RX=GPIO%d baud=%d",
             UART_SNORE_RX_RX, UART_SNORE_RX_BAUD);
    return true;
}

/* ── 对外接口 ── */
bool snore_report_rx_get_latest(snore_report_t *out)
{
    if (!out) return false;
    if (!s_latest.valid) {
        memset(out, 0, sizeof(*out));
        return false;
    }
    *out = s_latest;
    return true;
}

bool snore_report_is_fresh(uint32_t timeout_ms)
{
    if (!s_latest.valid) return false;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    return (now - s_last_update_ms) < timeout_ms;
}
