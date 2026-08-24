#include "wifi_mgr.h"

#include <string.h>
#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "wifi";

#define WIFI_CONNECTED_BIT  BIT0

static EventGroupHandle_t s_eg = NULL;
static wifi_mgr_cb_t s_cb = NULL;
static bool s_connected = false;

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "连接 Wi-Fi: %s", CONFIG_MOJI_WIFI_SSID);
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_connected) {
            s_connected = false;
            xEventGroupClearBits(s_eg, WIFI_CONNECTED_BIT);
            if (s_cb) s_cb(false);
        }
        ESP_LOGW(TAG, "Wi-Fi 断开，3 秒后重连...");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "已获取 IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_connected = true;
        xEventGroupSetBits(s_eg, WIFI_CONNECTED_BIT);
        if (s_cb) s_cb(true);
    }
}

esp_err_t wifi_mgr_start(wifi_mgr_cb_t cb)
{
    if (strlen(CONFIG_MOJI_WIFI_SSID) == 0) {
        ESP_LOGW(TAG, "未配置 SSID，跳过 Wi-Fi（menuconfig -> Moji2 配置）");
        return ESP_OK;
    }

    s_eg = xEventGroupCreate();
    s_cb = cb;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               wifi_event_handler, NULL));

    wifi_config_t wifi_cfg = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    strlcpy((char *)wifi_cfg.sta.ssid, CONFIG_MOJI_WIFI_SSID, sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, CONFIG_MOJI_WIFI_PASSWORD, sizeof(wifi_cfg.sta.password));
    if (strlen(CONFIG_MOJI_WIFI_PASSWORD) == 0) {
        wifi_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    return ESP_OK;
}

bool wifi_mgr_is_connected(void)
{
    return s_connected;
}
