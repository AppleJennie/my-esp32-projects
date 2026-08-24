/**
 * watch_ble_receiver.c — OV-Watch BLE SPP 接收 (NimBLE Central)
 *
 * 流程: 扫描 → 按名称匹配 → 连接 → 发现 Service/Char → 订阅 Notify
 *       → 每 500ms 发 OV+SEND → 解析 JSON → 写入全局 watch_data
 *       → 断线自动重连
 */
#include "watch_ble_receiver.h"
#include "sleep_data_center.h"
#include "app_role_config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

/* NimBLE */
#include <host/ble_hs.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/util/util.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>

static const char *TAG = "WATCH_BLE";

/* ═══════════════════════════════════════════════════════════════
 * 全局状态
 * ═══════════════════════════════════════════════════════════════ */

static struct {
    bool            initialized;
    bool            scanning;
    bool            connected;
    uint16_t        conn_handle;
    uint16_t        write_handle;    /* 写特征句柄 (发命令) */
    uint16_t        notify_handle;   /* Notify 特征句柄 */
    bool            notify_cccd_set; /* 是否已订阅 Notify */

    char            rx_buf[WATCH_RX_BUF_SIZE];
    int             rx_len;

    watch_ble_data_t latest;
    SemaphoreHandle_t data_mutex;

    /* 时间同步 */
    bool            time_synced;
    uint32_t        last_poll_ms;
    uint32_t        last_reconnect_ms;
} s_ctx;

static ble_addr_t s_watch_addr;

/* ═══════════════════════════════════════════════════════════════
 * JSON 解析 (轻量，不用 cJSON)
 * ═══════════════════════════════════════════════════════════════ */

static int json_get_int(const char *json, const char *key, int default_val)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(json, search);
    if (!p) return default_val;
    p += strlen(search);
    return atoi(p);
}

static void parse_ov_send_json(const char *json, watch_ble_data_t *out)
{
    memset(out, 0, sizeof(*out));

    /* 检查基本格式 */
    if (!strstr(json, "{")) return;

    out->hr       = (uint8_t)json_get_int(json, "hr", 0);
    out->spo2     = (uint8_t)json_get_int(json, "spo2", 0);
    out->spo2_is_ref = json_get_int(json, "spo2_ref", 0) == 1;
    out->posture  = (uint8_t)json_get_int(json, "posture", 0);

    /* 温湿度: 直接整数 */
    out->temp_x10 = (int16_t)(json_get_int(json, "temp", 0) * 10);
    out->humi_x10 = (int16_t)(json_get_int(json, "humi", 0) * 10);

    out->step     = (uint16_t)json_get_int(json, "step", 0);

    /* 电量: 如果有 battery 字段 */
    int bat = json_get_int(json, "battery", -1);
    if (bat >= 0) {
        out->battery = (uint8_t)bat;
        out->battery_valid = true;
    }

    out->valid = (out->hr > 0 || out->spo2 > 0);
    out->rx_timestamp = (uint32_t)(esp_timer_get_time() / 1000ULL);
}

/* ═══════════════════════════════════════════════════════════════
 * 发送命令 (GATT Write)
 * ═══════════════════════════════════════════════════════════════ */

static int watch_ble_write_cmd(const char *cmd)
{
    if (!s_ctx.connected || s_ctx.write_handle == 0) return -1;

    int ret = ble_gattc_write_flat(s_ctx.conn_handle, s_ctx.write_handle,
                                    cmd, strlen(cmd), NULL, NULL);
    if (ret != 0) {
        ESP_LOGW(TAG, "Write cmd failed: %d", ret);
    }
    return ret;
}

/* ═══════════════════════════════════════════════════════════════
 * NimBLE 回调
 * ═══════════════════════════════════════════════════════════════ */

static int watch_ble_gap_event(struct ble_gap_event *event, void *arg);

/* ── 扫描 → 连接 ── */
static void watch_ble_start_scan(void)
{
    if (s_ctx.scanning) return;

    /* 主动扫描参数 */
    struct ble_gap_disc_params params = {
        .itvl          = BLE_GAP_SCAN_FAST_INTERVAL_MIN,  /* 30ms */
        .window        = BLE_GAP_SCAN_FAST_WINDOW,        /* 30ms */
        .filter_policy = BLE_HCI_SCAN_FILT_NO_WL,
        .limited       = 0,
        .passive       = 0,  /* 主动扫描 */
        .filter_duplicates = 1,
    };

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, 0, &params,
                           watch_ble_gap_event, NULL);
    if (rc == 0) {
        s_ctx.scanning = true;
        ESP_LOGI(TAG, "Scanning for '%s'...", WATCH_BLE_NAME_PREFIX);
    } else {
        ESP_LOGE(TAG, "Scan start failed: %d", rc);
    }
}

static void watch_ble_stop_scan(void)
{
    if (!s_ctx.scanning) return;
    ble_gap_disc_cancel();
    s_ctx.scanning = false;
}

/* ── 发现特征 ── */
static int watch_ble_on_disc_chr(uint16_t conn_handle, uint16_t attr_handle,
                                  const struct ble_gatt_error *error,
                                  const struct ble_gatt_chr *chr, void *arg)
{
    if (error->status != 0) return 0;

    /* 匹配 Write UUID: 发命令用 */
    if (chr->uuid.u.type == BLE_UUID_TYPE_16 &&
        chr->uuid.u16.value == WATCH_BLE_WRITE_UUID) {
        s_ctx.write_handle = chr->val_handle;
        ESP_LOGI(TAG, "Found Write char handle=0x%04x", s_ctx.write_handle);
    }

    /* 检查 Notify 属性 */
    if (chr->properties & BLE_GATT_CHR_PROP_NOTIFY) {
        s_ctx.notify_handle = chr->val_handle;
        ESP_LOGI(TAG, "Found Notify char handle=0x%04x", s_ctx.notify_handle);
    }

    return 0;
}

/* ── 订阅 Notify 回调 ── */
static int watch_ble_on_notify(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_NOTIFY_EVENT &&
        ctxt->op != BLE_GATT_ACCESS_OP_READ_INDICATION_EVENT) {
        return 0;
    }

    int data_len = OS_MBUF_PKTLEN(ctxt->om);
    if (data_len <= 0 || data_len >= WATCH_RX_BUF_SIZE) return 0;

    /* 拼接到接收缓冲 */
    int room = WATCH_RX_BUF_SIZE - s_ctx.rx_len - 1;
    if (room <= 0) {
        s_ctx.rx_len = 0;  /* 溢出，丢弃重建 */
    }
    int n = ble_hs_mbuf_to_flat(ctxt->om, (uint8_t *)(s_ctx.rx_buf + s_ctx.rx_len),
                                 room, NULL);
    if (n > 0) {
        s_ctx.rx_len += n;
        s_ctx.rx_buf[s_ctx.rx_len] = '\0';
    }

    /* 检查 JSON 完整性: 找到配对的 {} */
    if (strchr(s_ctx.rx_buf, '{') && strchr(s_ctx.rx_buf, '}')) {
        s_ctx.rx_buf[s_ctx.rx_len] = '\0';

        /* 提取最外层 JSON */
        char *start = strchr(s_ctx.rx_buf, '{');
        char *end   = strrchr(s_ctx.rx_buf, '}');
        if (start && end && end > start) {
            *(end + 1) = '\0';

            watch_ble_data_t d;
            parse_ov_send_json(start, &d);

            if (d.valid) {
                xSemaphoreTake(s_ctx.data_mutex, portMAX_DELAY);
                s_ctx.latest = d;
                xSemaphoreGive(s_ctx.data_mutex);

                /* 仅血氧喂入融合系统（心率和体动用雷达的） */
                if (d.spo2 > 0) {
                    SleepDataCenter::GetInstance().UpdateSpo2((float)d.spo2);
                }
            }

            ESP_LOGI(TAG, "BLE DATA: spo2=%d%%%s hr=%d temp=%.1f°C humi=%.1f%% step=%d posture=%d",
                     d.spo2, d.spo2_is_ref ? "(ref)" : "",
                     d.hr,
                     (float)d.temp_x10 / 10.0f, (float)d.humi_x10 / 10.0f,
                     d.step, d.posture);
        }
        s_ctx.rx_len = 0;
    }

    return 0;
}

/* ── GAP 事件处理 ── */
static int watch_ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {

    case BLE_GAP_EVENT_DISC: {
        /* 扫描到设备 */
        struct ble_gap_disc_desc *d = &event->disc;
        const char *name = (const char *)d->name.name;
        int name_len = d->name.name_len;

        if (name_len > 0 && name != NULL &&
            strncmp(name, WATCH_BLE_NAME_PREFIX,
                    strlen(WATCH_BLE_NAME_PREFIX)) == 0) {
            ESP_LOGI(TAG, "Found watch: %.*s RSSI=%d addr=%02x:%02x:...",
                     name_len, name, d->rssi,
                     d->addr.val[0], d->addr.val[1]);

            watch_ble_stop_scan();

            memcpy(&s_watch_addr, &d->addr, sizeof(ble_addr_t));

            /* 发起连接 */
            int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &d->addr,
                                      0, NULL, watch_ble_gap_event, NULL);
            if (rc != 0) {
                ESP_LOGE(TAG, "Connect failed: %d", rc);
                watch_ble_start_scan();
            }
        }
        break;
    }

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_ctx.conn_handle = event->connect.conn_handle;
            s_ctx.connected = true;
            s_ctx.last_poll_ms = 0;
            s_ctx.time_synced = false;
            s_ctx.rx_len = 0;
            s_ctx.write_handle = 0;
            s_ctx.notify_handle = 0;
            s_ctx.notify_cccd_set = false;

            ESP_LOGI(TAG, "★★★ 手表已连接! handle=%d ★★★", s_ctx.conn_handle);

            /* 发现服务 */
            ble_gattc_disc_all_chrs(s_ctx.conn_handle,
                                    1, 0xFFFF,
                                    watch_ble_on_disc_chr, NULL);
        } else {
            ESP_LOGW(TAG, "Connect failed status=%d, will retry",
                     event->connect.status);
            s_ctx.connected = false;
            s_ctx.last_reconnect_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGW(TAG, "Disconnected reason=%d, will reconnect",
                 event->disconnect.reason);
        s_ctx.connected = false;
        s_ctx.write_handle = 0;
        s_ctx.notify_handle = 0;
        s_ctx.notify_cccd_set = false;
        s_ctx.last_reconnect_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        break;

    case BLE_GAP_EVENT_NOTIFY_RX:
        /* NimBLE 自动路由到特征回调，这里不需要处理 */
        break;

    default:
        break;
    }
    return 0;
}

/* ── NimBLE 主机就绪回调 ── */
static void watch_ble_on_sync(void)
{
    ESP_LOGI(TAG, "NimBLE host ready, starting scan");
    watch_ble_start_scan();
}

/* ── NimBLE 主机重置回调 ── */
static void watch_ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE host reset, reason=%d", reason);
}

/* ═══════════════════════════════════════════════════════════════
 * 轮询任务
 * ═══════════════════════════════════════════════════════════════ */

static TaskHandle_t s_poll_task = NULL;
static bool s_poll_running = false;

static void watch_ble_poll_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Poll task started");

    while (s_poll_running) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);

        /* 已连接 → 轮询 */
        if (s_ctx.connected && s_ctx.write_handle != 0) {

            /* 首次连接后同步时间 */
            if (!s_ctx.time_synced) {
                time_t t = time(NULL);
                struct tm *tm = localtime(&t);
                char time_cmd[32];
                snprintf(time_cmd, sizeof(time_cmd),
                         "OV+ST=%04d%02d%02d%02d%02d%02d",
                         tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                         tm->tm_hour, tm->tm_min, tm->tm_sec);
                watch_ble_write_cmd(time_cmd);
                s_ctx.time_synced = true;
                ESP_LOGI(TAG, "Time sync sent: %s", time_cmd);
            }

            /* 定时轮询 OV+SEND */
            if (now - s_ctx.last_poll_ms >= WATCH_POLL_INTERVAL_MS) {
                watch_ble_write_cmd("OV+SEND");
                s_ctx.last_poll_ms = now;
            }

            /* 自动订阅 Notify (如果还没订阅) */
            if (!s_ctx.notify_cccd_set && s_ctx.notify_handle != 0) {
                uint8_t val[] = {0x01, 0x00};  /* CCCD: enable notification */
                ble_gattc_write_flat(s_ctx.conn_handle,
                                      s_ctx.notify_handle + 1,  /* CCCD descriptor */
                                      val, sizeof(val),
                                      NULL, NULL);
                s_ctx.notify_cccd_set = true;
                ESP_LOGI(TAG, "Notify subscribed");
            }
        }

        /* 断线重连 */
        if (!s_ctx.connected &&
            now - s_ctx.last_reconnect_ms >= WATCH_RECONNECT_INTERVAL_MS) {
            ESP_LOGI(TAG, "Reconnecting...");
            s_ctx.last_reconnect_ms = now;
            watch_ble_start_scan();
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }

    s_poll_task = NULL;
    vTaskDelete(NULL);
}

/* ═══════════════════════════════════════════════════════════════
 * 公开 API
 * ═══════════════════════════════════════════════════════════════ */

int watch_ble_receiver_init(void)
{
    if (s_ctx.initialized) return 0;

    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.data_mutex = xSemaphoreCreateMutex();
    if (!s_ctx.data_mutex) {
        ESP_LOGE(TAG, "Mutex create failed");
        return -1;
    }

    /* NimBLE 移植初始化 */
    nimble_port_init();

    /* 配置主机回调 */
    ble_hs_cfg.sync_cb  = watch_ble_on_sync;
    ble_hs_cfg.reset_cb = watch_ble_on_reset;

    /* 内存配置 */
    ble_hs_cfg.max_connections    = 1;
    ble_hs_cfg.max_attrs          = 32;
    ble_hs_cfg.max_services       = 4;
    ble_hs_cfg.max_client_configs = 1;
    ble_hs_cfg.max_gattc_procs    = 4;

    /* GATT 回调：接收 Notify */
    ble_hs_cfg.gattc_cb.notify_cb = watch_ble_on_notify;
    ble_hs_cfg.gattc_cb.write_cb  = NULL;

    /* 启动 NimBLE FreeRTOS 任务 */
    nimble_port_freertos_init(NULL);

    s_ctx.initialized = true;

    /* 启动轮询任务 */
    s_poll_running = true;
    xTaskCreate(watch_ble_poll_task, "watch_poll", 4096, NULL, 4, &s_poll_task);

    ESP_LOGI(TAG, "BLE receiver init OK, target='%s' poll=%dms",
             WATCH_BLE_NAME_PREFIX, WATCH_POLL_INTERVAL_MS);
    return 0;
}

void watch_ble_receiver_deinit(void)
{
    s_poll_running = false;
    if (s_ctx.connected) {
        ble_gap_terminate(s_ctx.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    if (s_ctx.scanning) {
        watch_ble_stop_scan();
    }
    nimble_port_deinit();
    s_ctx.initialized = false;
    ESP_LOGI(TAG, "BLE receiver deinit");
}

bool watch_ble_get_latest(watch_ble_data_t *out)
{
    if (!out || !s_ctx.data_mutex) return false;
    xSemaphoreTake(s_ctx.data_mutex, portMAX_DELAY);
    *out = s_ctx.latest;
    xSemaphoreGive(s_ctx.data_mutex);
    return out->valid;
}

bool watch_ble_is_connected(void)
{
    return s_ctx.connected;
}
