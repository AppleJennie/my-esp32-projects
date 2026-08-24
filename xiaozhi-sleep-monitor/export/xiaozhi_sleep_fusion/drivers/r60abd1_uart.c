/**
 * r60abd1_uart.c — R60ABD1 60GHz 雷达 UART 驱动 v3 (polling)
 *
 * 帧格式：53 59 + ctrl + cmd + lenH + lenL + payload + checksum + 54 43
 *
 * v3 改动：
 *   - 移除 UART event queue，改为 uart_read_bytes 轮询
 *   - 逐字节 feed 到 parser，边界保护
 *   - r60_uart_task pin CPU1，避免 CPU0 WDT
 *   - 每 5 秒打印 alive 状态
 */

#include "r60abd1_uart.h"
#include "sleep_radar_data.h"
#include "sleep_algorithm.h"
#include "sleep_logger.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "string.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "R60_UART";

/* ── Parser 状态 ── */
#define R60_PARSE_IDLE      0
#define R60_PARSE_GOT_53    1
#define R60_PARSE_GOT_59    2
#define R60_PARSE_GOT_CTRL  3
#define R60_PARSE_GOT_CMD   4
#define R60_PARSE_GOT_LENH  5
#define R60_PARSE_GOT_LENL  6

static struct {
    uint8_t  state;
    uint8_t  ctrl;
    uint8_t  cmd;
    uint16_t data_len;
    uint16_t data_idx;
    uint8_t  data_buf[R60ABD1_MAX_PAYLOAD_LEN];  /* max 128 bytes */
    uint8_t  ck_rx;
    uint32_t frame_count;
    uint32_t valid_count;
    uint32_t bad_count;
    uint32_t overflow_count;
} s_parser;

/* ── 统计 ── */
static uint32_t s_rx_bytes    = 0;
static uint32_t s_last_alive  = 0;

/* ═══════════════════════════════════════════════════════════════
 * 校验和
 * ═══════════════════════════════════════════════════════════════ */

static uint8_t calc_checksum(uint8_t ctrl, uint8_t cmd,
                              uint16_t data_len, const uint8_t *data)
{
    uint16_t sum = 0x53 + 0x59; /* frame head */
    sum += ctrl; sum += cmd;
    sum += (data_len >> 8) & 0xFF;
    sum += (data_len >> 0) & 0xFF;
    for (uint16_t i = 0; i < data_len; i++) sum += data[i];
    return (uint8_t)(sum & 0xFF);
}

/* ═══════════════════════════════════════════════════════════════
 * 帧解析——dispatch
 * ═══════════════════════════════════════════════════════════════ */

static void dispatch_frame(uint8_t ctrl, uint8_t cmd,
                            uint16_t len, const uint8_t *p, uint32_t now)
{
    /* ── 0x01 system ── */
    if (ctrl == 0x01 && cmd == 0x01 && len >= 1) {
        sleep_radar_data_update_heartbeat(p[0], now);
        return;
    }

    /* ── 0x80 presence ── */
    if (ctrl == 0x80) {
        switch (cmd) {
            case 0x01: case 0x81: if (len>=1) sleep_radar_data_update_presence(p[0], now); return;
            case 0x02:            if (len>=1) sleep_radar_data_update_motion_state(p[0], now); return;
            case 0x03:            if (len>=1) sleep_radar_data_update_body_motion(p[0], now); return;
            case 0x04:            if (len>=2) { uint16_t d=((uint16_t)p[0]<<8)|p[1]; sleep_radar_data_update_distance(d, now); } return;
            case 0x05:            if (len>=6) {
                int16_t x=(int16_t)(((uint16_t)p[0]<<8)|p[1]);
                int16_t y=(int16_t)(((uint16_t)p[2]<<8)|p[3]);
                int16_t z=(int16_t)(((uint16_t)p[4]<<8)|p[5]);
                sleep_radar_data_update_target_3d(x,y,z,now);
            } return;
            default: return;
        }
    }

    /* ── 0x81 breath ── */
    if (ctrl == 0x81) {
        switch (cmd) {
            case 0x01: case 0x81: if (len>=1) sleep_radar_data_update_breath_status(p[0], now); return;
            case 0x02: {
                if (len>=1) { uint8_t br=p[0]; if (br>128) br-=128; sleep_radar_data_update_breath_rate(br,now); }
            } return;
            case 0x05: case 0x85: sleep_radar_data_update_breath_wave_buffer(p, (uint8_t)len, now); return;
            default: return;
        }
    }

    /* ── 0x84 sleep (in_bed only) ── */
    if (ctrl == 0x84) {
        if ((cmd == 0x01 || cmd == 0x81) && len >= 1) {
            sleep_radar_data_update_in_bed(p[0], now);
        }
        return;
    }

    /* ── 0x85 heart ── */
    if (ctrl == 0x85) {
        switch (cmd) {
            case 0x02: if (len>=1) sleep_radar_data_update_heart_rate(p[0], now); return;
            case 0x05: case 0x85: sleep_radar_data_update_heart_wave_buffer(p, (uint8_t)len, now); return;
            default: return;
        }
    }

    /* ── 0x07 vendor ── */
    if (ctrl == 0x07 && len >= 1) {
        sleep_radar_data_update_vendor_status(p[0], now);
        return;
    }

    /* unknown — 静默丢弃 */
}

/* ═══════════════════════════════════════════════════════════════
 * feed_byte — 逐字节帧解析器（边界保护）
 * ═══════════════════════════════════════════════════════════════ */

static void r60abd1_parser_feed_byte(uint8_t b)
{
    switch (s_parser.state) {

        case R60_PARSE_IDLE:
            if (b == 0x53) { s_parser.state = R60_PARSE_GOT_53; }
            break;

        case R60_PARSE_GOT_53:
            if (b == 0x59) { s_parser.state = R60_PARSE_GOT_59; }
            else if (b == 0x53) { /* stay */ }
            else { s_parser.state = R60_PARSE_IDLE; }
            break;

        case R60_PARSE_GOT_59:
            s_parser.ctrl = b;
            s_parser.state = R60_PARSE_GOT_CTRL;
            break;

        case R60_PARSE_GOT_CTRL:
            s_parser.cmd = b;
            s_parser.state = R60_PARSE_GOT_CMD;
            break;

        case R60_PARSE_GOT_CMD:
            s_parser.data_len = ((uint16_t)b << 8);
            s_parser.state = R60_PARSE_GOT_LENH;
            break;

        case R60_PARSE_GOT_LENH:
            s_parser.data_len |= b;
            /* 边界保护：长度不能超过 buffer */
            if (s_parser.data_len > R60ABD1_MAX_PAYLOAD_LEN) {
                s_parser.overflow_count++;
                s_parser.bad_count++;
                s_parser.state = R60_PARSE_IDLE;  /* 丢弃，重新找帧头 */
            } else {
                s_parser.data_idx = 0;
                s_parser.state = R60_PARSE_GOT_LENL;
            }
            break;

        case R60_PARSE_GOT_LENL:
            /* 接收 payload */
            if (s_parser.data_idx < s_parser.data_len) {
                s_parser.data_buf[s_parser.data_idx++] = b;
                if (s_parser.data_idx >= s_parser.data_len) {
                    /* 下一个字节是 checksum */
                    s_parser.state = R60_PARSE_IDLE;  /* checksum 在下一次 feed */
                    /* 保存 checksum 预期位置 */
                }
            }
            break;
    }

    /* 当 payload 收完后的 checksum 处理：
     * 我们无法在状态机里直接跳到 checksum，因为 payload 长度可变。
     * 改用：在 IDLE/GOT_53/GOT_59 检查中顺带处理 */

    /* 简化策略：payload 收完后不切换状态，而是继续收 checksum + tail */
    if (s_parser.data_idx >= s_parser.data_len && s_parser.data_len > 0
        && s_parser.state == R60_PARSE_IDLE) {
        /* 上一个字节是 checksum — 已经在 feed 时保存为 ck_rx */
        /* 不对，需要重新设计。让我用更简单的办法——直接在这里收完 frame */
    }
}

/* ═══════════════════════════════════════════════════════════════
 * 简化版解析：直接收整帧（不用逐字节状态机）
 * 用 ring buffer + 帧头搜索，最稳定
 * ═══════════════════════════════════════════════════════════════ */

#define R60_RING_SIZE  512
static uint8_t  s_ring[R60_RING_SIZE];
static uint16_t s_ring_wr  = 0;
static uint16_t s_ring_cnt = 0;

static void ring_push(uint8_t b) {
    s_ring[s_ring_wr] = b;
    s_ring_wr = (s_ring_wr + 1) % R60_RING_SIZE;
    if (s_ring_cnt < R60_RING_SIZE) s_ring_cnt++;
}

static uint8_t ring_get(int offset) {
    /* offset=0 是最旧字节, offset=cnt-1 是最新字节 */
    int idx = (s_ring_wr - s_ring_cnt + offset + R60_RING_SIZE) % R60_RING_SIZE;
    return s_ring[idx];
}

static void ring_consume(int n) {
    if (n >= (int)s_ring_cnt) { s_ring_cnt = 0; return; }
    s_ring_cnt -= (uint16_t)n;
}

/* ── 在 ring buffer 中搜索并提取有效帧 ── */
static int try_extract_frame(void)
{
    if (s_ring_cnt < 9) return 0;  /* 最小帧: head(2)+ctrl(1)+cmd(1)+len(2)+ck(1)+tail(2)=9 */

    /* 搜索帧头 53 59 */
    int head_pos = -1;
    for (int i = 0; i < (int)s_ring_cnt - 1; i++) {
        if (ring_get(i) == 0x53 && ring_get(i + 1) == 0x59) {
            head_pos = i; break;
        }
    }

    if (head_pos < 0) {
        /* 没有帧头，丢弃所有除最后一个字节（可能是 0x53）以外的数据 */
        int drop = (int)s_ring_cnt - 1;
        if (ring_get(s_ring_cnt - 1) == 0x53) {
            if (drop > 0) ring_consume(drop);
        } else {
            ring_consume((int)s_ring_cnt);
        }
        return 0;
    }

    /* 丢弃帧头前的垃圾 */
    if (head_pos > 0) {
        ring_consume(head_pos);
        head_pos = 0;
    }

    /* 检查是否有足够的字节 */
    if (s_ring_cnt < 9) return 0;

    uint8_t  ctrl     = ring_get(2);
    uint8_t  cmd      = ring_get(3);
    uint16_t data_len = ((uint16_t)ring_get(4) << 8) | ring_get(5);
    uint16_t frame_total = 9 + data_len;  /* head(2)+ctrl(1)+cmd(1)+len(2)+data+ck(1)+tail(2) */

    /* 边界保护 */
    if (data_len > R60ABD1_MAX_PAYLOAD_LEN) {
        s_parser.bad_count++;
        ring_consume(2);  /* 跳过 53 59，重新搜索 */
        return 0;
    }

    if (s_ring_cnt < frame_total) return 0;  /* 数据未收完 */

    /* 检查帧尾 */
    if (ring_get(frame_total - 2) != 0x54 || ring_get(frame_total - 1) != 0x43) {
        s_parser.bad_count++;
        ring_consume(2);
        return 0;
    }

    /* 提取 payload */
    uint8_t payload_buf[R60ABD1_MAX_PAYLOAD_LEN];
    for (uint16_t i = 0; i < data_len; i++) {
        payload_buf[i] = ring_get(6 + i);
    }
    uint8_t rx_ck = ring_get(6 + data_len);

    /* 校验 */
    uint8_t calc_ck = calc_checksum(ctrl, cmd, data_len, payload_buf);
    if (rx_ck != calc_ck) {
        s_parser.bad_count++;
        ring_consume(2);
        return 0;
    }

    /* 有效帧 */
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    dispatch_frame(ctrl, cmd, data_len, payload_buf, now);
    sleep_logger_notify_raw_frame();
    s_parser.valid_count++;
    s_parser.frame_count++;

    ring_consume((int)frame_total);
    return 1;
}

/* ═══════════════════════════════════════════════════════════════
 * UART 接收任务（polling，无 event queue）
 * ═══════════════════════════════════════════════════════════════ */

static void r60abd1_uart_task(void *pvParameters)
{
    uint8_t rx_buf[128];
    uint32_t last_alive = 0;

    ESP_LOGI(TAG, "R60 UART task started (CPU%d, polling mode)",
             xPortGetCoreID());

    while (1) {
        /* Polling read — 50ms timeout */
        int len = uart_read_bytes(R60ABD1_UART_NUM, rx_buf, sizeof(rx_buf),
                                   pdMS_TO_TICKS(50));

        if (len > 0) {
            s_rx_bytes += (uint32_t)len;
            for (int i = 0; i < len; i++) {
                ring_push(rx_buf[i]);
            }
            /* 尝试从 ring buffer 提取所有完整帧 */
            int extracted = 0;
            do { extracted = try_extract_frame(); } while (extracted > 0);
        } else if (len < 0 && len != -1) {
            /* -1 = timeout (normal), other negatives = error */
            ESP_LOGW(TAG, "uart_read_bytes error=%d", len);
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        /* 每 5 秒 alive */
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (now - last_alive >= 5000) {
            last_alive = now;
            ESP_LOGI(TAG, "[R60_UART] alive rx_bytes=%lu valid=%lu bad=%lu "
                     "overflow=%lu ring=%u/%u stack_hwm=%lu",
                     (unsigned long)s_rx_bytes,
                     (unsigned long)s_parser.valid_count,
                     (unsigned long)s_parser.bad_count,
                     (unsigned long)s_parser.overflow_count,
                     (unsigned)s_ring_cnt, (unsigned)R60_RING_SIZE,
                     (unsigned long)uxTaskGetStackHighWaterMark(NULL));
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/* ═══════════════════════════════════════════════════════════════
 * 公共接口
 * ═══════════════════════════════════════════════════════════════ */

esp_err_t r60abd1_uart_init(void)
{
    memset(&s_parser, 0, sizeof(s_parser));
    memset(s_ring, 0, sizeof(s_ring));
    s_ring_wr = 0; s_ring_cnt = 0;
    s_rx_bytes = 0;

    uart_config_t cfg = {
        .baud_rate  = R60ABD1_UART_BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    /* 安装 UART 驱动——不需要 event queue */
    esp_err_t ret = uart_driver_install(R60ABD1_UART_NUM,
                                         R60ABD1_UART_BUF_SIZE,
                                         R60ABD1_UART_BUF_SIZE,
                                         0,        /* 不需要 event queue */
                                         NULL,      /* 不需要 queue handle */
                                         0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install fail: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_param_config(R60ABD1_UART_NUM, &cfg);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "config fail"); return ret; }

    ret = uart_set_pin(R60ABD1_UART_NUM,
                        R60ABD1_UART_TXD_GPIO, R60ABD1_UART_RXD_GPIO,
                        UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "pin fail"); return ret; }

    ESP_LOGI(TAG, "UART init OK TX=%d RX=%d %d baud (polling, no event queue)",
             R60ABD1_UART_TXD_GPIO, R60ABD1_UART_RXD_GPIO,
             R60ABD1_UART_BAUD_RATE);
    return ESP_OK;
}

esp_err_t r60abd1_uart_start_task(void)
{
    /* Pin 到 CPU1，避免干扰 CPU0 的音频管线 */
    BaseType_t ok = xTaskCreatePinnedToCore(
        r60abd1_uart_task, "r60_uart",
        6144,          /* 栈: 6KB */
        NULL,
        8,             /* 优先级 */
        NULL,
        1              /* CPU1 */
    );
    return (ok == pdPASS) ? ESP_OK : ESP_FAIL;
}

void r60abd1_uart_stop_task(void)
{
    /* 任务自己管理生命周期，需要时可通过全局标志停止 */
}

int r60abd1_uart_send(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0) return 0;
    return uart_write_bytes(R60ABD1_UART_NUM, (const char *)data, len);
}

/* ═══════════════════════════════════════════════════════════════
 * 初始化命令
 * ═══════════════════════════════════════════════════════════════ */

void r60abd1_send_init_commands(void)
{
    ESP_LOGI(TAG, "Sending radar init commands...");
    const uint8_t c1[] = R60_INIT_PRESENCE;  r60abd1_uart_send(c1, 10); vTaskDelay(pdMS_TO_TICKS(200));
    const uint8_t c2[] = R60_INIT_BREATH;    r60abd1_uart_send(c2, 10); vTaskDelay(pdMS_TO_TICKS(200));
    const uint8_t c3[] = R60_INIT_HEART;     r60abd1_uart_send(c3, 10); vTaskDelay(pdMS_TO_TICKS(200));
    ESP_LOGI(TAG, "Init done: presence+breath+heart enabled");
}

/* ═══════════════════════════════════════════════════════════════
 * 查询任务（不变——定期查询波形和状态）
 * ═══════════════════════════════════════════════════════════════ */

void r60abd1_query_task(void *pvParameters)
{
    const uint8_t q_bw[] = R60_QUERY_BREATH_WAVE;
    const uint8_t q_hw[] = R60_QUERY_HEART_WAVE;
    const uint8_t q_pr[] = R60_QUERY_PRESENCE;
    const uint8_t q_bs[] = R60_QUERY_BREATH_STAT;
    const uint8_t q_ib[] = R60_QUERY_IN_BED;

    uint32_t last_1s = 0, last_3s = 0;

    ESP_LOGI(TAG, "Query task: waiting %d ms warmup...", R60ABD1_INIT_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(R60ABD1_INIT_DELAY_MS));
    r60abd1_send_init_commands();
    ESP_LOGI(TAG, "Query task: radar ready");

    while (1) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        /* 每 1 秒：波形 */
        if (now - last_1s >= 1000) {
            last_1s = now;
            r60abd1_uart_send(q_bw, 10);
            r60abd1_uart_send(q_hw, 10);
        }

        /* 每 3 秒：基础状态 */
        if (now - last_3s >= 3000) {
            last_3s = now;
            sleep_radar_data_t *d = sleep_radar_data_get();
            uint32_t f5 = now - 5000;
            if (d->last_presence_ms == 0 || d->last_presence_ms < f5)
                r60abd1_uart_send(q_pr, 10);
            if (d->last_breath_status_ms == 0 || d->last_breath_status_ms < f5)
                r60abd1_uart_send(q_bs, 10);
            if (d->last_inbed_update_ms == 0 || d->last_inbed_update_ms < now - 10000)
                r60abd1_uart_send(q_ib, 10);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/* ── 兼容旧 API ── */
void r60abd1_handle_frame(const r60abd1_frame_t *frame) { (void)frame; }
void r60abd1_debug_print_raw(const uint8_t *buf, uint16_t len) { (void)buf; (void)len; }
