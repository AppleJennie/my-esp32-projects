/**
 * snore_report_rx.c — UI 板 SNORE 文本接收实现（线程安全版）
 *
 * UART2 RX 按行接收 → 解析 SNORE 协议 → mutex 保护 latest 快照。
 */
#include "snore_report_rx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "sleep_data_center.h"

static const char *TAG = "SNORE_RX";

#define SNORE_LINE_BUF_SIZE  512   /* 连接瞬间可能有噪声 */
#define SNORE_TASK_STACK     4096
#define SNORE_TASK_PRIO      3
#define SNORE_UART_RX_BUF    2048

static snore_report_t s_latest;
static uint32_t s_last_update_ms = 0;
static SemaphoreHandle_t s_lock = NULL;
static TaskHandle_t s_task = NULL;
static bool s_started = false;

static uint8_t u8_sat(unsigned int v) { return (v > 255U) ? 255U : (uint8_t)v; }
static uint16_t u16_sat(unsigned int v) { return (v > 65535U) ? 65535U : (uint16_t)v; }

static bool parse_snore_line(const char *line, int len, snore_report_t *out)
{
    if (!line || !out || len < 10) return false;
    if (strncmp(line, "SNORE,", 6) != 0) return false;

    unsigned long ts = 0, cur_ep = 0, total = 0, longest = 0;
    unsigned int mic_ok = 0, audio_valid = 0, snore_active = 0, snore_score = 0;
    unsigned int snore_db = 0, rms = 0, peak = 0, zcr_x100 = 0;
    unsigned int ep_count = 0, quality = 0;

    int n = sscanf(line,
        "SNORE,%lu,%u,%u,%u,%u,%u,%u,%u,%u,%lu,%lu,%lu,%u,%u",
        &ts,
        &mic_ok,
        &audio_valid,
        &snore_active,
        &snore_score,
        &snore_db,
        &rms,
        &peak,
        &zcr_x100,
        &cur_ep,
        &total,
        &longest,
        &ep_count,
        &quality);

    if (n < 14) {
        ESP_LOGW(TAG, "SNORE parse failed: got %d fields (need >=14): %.*s", n,
                 (len < 96 ? len : 96), line);
        return false;
    }
    /* n>=14: 基础字段正常; n>=21: 高级字段预留 */

    memset(out, 0, sizeof(*out));
    out->ts_ms = (uint32_t)ts;
    out->mic_ok = u8_sat(mic_ok);
    out->audio_valid = u8_sat(audio_valid);
    out->snore_active = u8_sat(snore_active);
    out->snore_score = u8_sat(snore_score);
    out->snore_db = u8_sat(snore_db);
    out->rms = u16_sat(rms);
    out->peak = u16_sat(peak);
    out->zcr_x100 = u8_sat(zcr_x100);
    out->current_episode_ms = (uint32_t)cur_ep;
    out->snore_total_ms = (uint32_t)total;
    out->longest_episode_ms = (uint32_t)longest;
    out->snore_episode_count = u16_sat(ep_count);
    out->quality = u8_sat(quality);
    out->rx_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    out->valid = true;
    return true;
}

static void latest_store(const snore_report_t *r)
{
    if (!r) return;
    if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_latest = *r;
        s_last_update_ms = r->rx_ms;
        xSemaphoreGive(s_lock);
    }
}

static void snore_rx_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Task started, UART%d RX=GPIO%d TX=GPIO%d baud=%d",
             UART_SNORE_RX_NUM, UART_SNORE_RX_RX, UART_SNORE_RX_TX, UART_SNORE_RX_BAUD);

    char line_buf[SNORE_LINE_BUF_SIZE];
    int line_pos = 0;
    uint32_t report_count = 0;
    uint8_t rx_buf[96];

    while (1) {
        int len = uart_read_bytes(UART_SNORE_RX_NUM, rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(100));
        if (len <= 0) {
            continue;
        }

        for (int i = 0; i < len; ++i) {
            uint8_t ch = rx_buf[i];
            if (ch == '\n' || ch == '\r') {
                if (line_pos > 0) {
                    line_buf[line_pos] = '\0';

                    /* SPO2 血氧行: "SPO2,98" */
                    if (strncmp(line_buf, "SPO2,", 5) == 0) {
                        int spo2 = atoi(line_buf + 5);
                        if (spo2 > 0 && spo2 <= 100) {
                            SleepDataCenter::GetInstance().UpdateSpo2((float)spo2);
                            ESP_LOGD(TAG, "SPO2 updated: %d%%", spo2);
                        }
                        line_pos = 0;
                        continue;
                    }

                    snore_report_t report;
                    if (parse_snore_line(line_buf, line_pos, &report)) {
                        latest_store(&report);
                        ++report_count;
                        if ((report_count % 10U) == 0U) {
                            ESP_LOGI(TAG, "SNORE reports=%lu mic=%u valid=%u snore=%u score=%u",
                                     (unsigned long)report_count,
                                     report.mic_ok, report.audio_valid,
                                     report.snore_active, report.snore_score);
                        }
                    }
                    line_pos = 0;
                }
            } else {
                if (line_pos < (SNORE_LINE_BUF_SIZE - 1)) {
                    line_buf[line_pos++] = (char)ch;
                } else {
                    ESP_LOGW(TAG, "SNORE line too long, dropped");
                    line_pos = 0;
                }
            }
        }
    }
}

bool snore_report_rx_start(void)
{
    if (s_started) return true;

    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            ESP_LOGE(TAG, "mutex create failed");
            return false;
        }
    }

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

    err = uart_driver_install(UART_SNORE_RX_NUM, SNORE_UART_RX_BUF, 0, 0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return false;
    }

    if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        memset(&s_latest, 0, sizeof(s_latest));
        s_last_update_ms = 0;
        xSemaphoreGive(s_lock);
    }

    BaseType_t ret = xTaskCreate(snore_rx_task, "snore_rx",
                                 SNORE_TASK_STACK, NULL,
                                 SNORE_TASK_PRIO, &s_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Task create failed");
        return false;
    }

    s_started = true;
    ESP_LOGI(TAG, "Init OK, RX=GPIO%d TX=GPIO%d baud=%d",
             UART_SNORE_RX_RX, UART_SNORE_RX_TX, UART_SNORE_RX_BAUD);
    return true;
}

bool snore_report_rx_get_latest(snore_report_t *out)
{
    if (!out) return false;
    bool ok = false;
    if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (s_latest.valid) {
            *out = s_latest;
            ok = true;
        } else {
            memset(out, 0, sizeof(*out));
        }
        xSemaphoreGive(s_lock);
    } else {
        memset(out, 0, sizeof(*out));
    }
    return ok;
}

bool snore_report_is_fresh(uint32_t timeout_ms)
{
    bool fresh = false;
    uint32_t last_ms = 0;
    bool valid = false;

    if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        last_ms = s_last_update_ms;
        valid = s_latest.valid;
        xSemaphoreGive(s_lock);
    }

    if (valid && last_ms != 0) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
        fresh = ((uint32_t)(now - last_ms) < timeout_ms);
    }
    return fresh;
}
