#include "hardware_control.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "sleep_project_config.h"
#if CONFIG_ENABLE_WIFI
#include "wifi_user.h"
#endif
#if CONFIG_ENABLE_MUSIC_PLAYER
#include "music_user.h"
#endif
#include "ltdc.h"

#define TAG "hw_ctrl"
#define HW_CMD_QUEUE_LEN  16

static QueueHandle_t s_hw_cmd_queue = NULL;

static const char *cmd_to_str(hardware_cmd_id_t cmd)
{
    switch (cmd) {
        case HW_CMD_MONITOR_START:      return "MONITOR_START";
        case HW_CMD_MONITOR_STOP:       return "MONITOR_STOP";
        case HW_CMD_DND_ON:             return "DND_ON";
        case HW_CMD_DND_OFF:            return "DND_OFF";
        case HW_CMD_WIFI_ON:            return "WIFI_ON";
        case HW_CMD_WIFI_OFF:           return "WIFI_OFF";
        case HW_CMD_SET_BRIGHTNESS:     return "SET_BRIGHTNESS";
        case HW_CMD_SET_VOLUME:         return "SET_VOLUME";
        case HW_CMD_LCD_SCREEN_OFF:     return "LCD_SCREEN_OFF";
        case HW_CMD_LCD_SCREEN_ON:      return "LCD_SCREEN_ON";
        case HW_CMD_RADAR_RESET:        return "RADAR_RESET";
        case HW_CMD_RADAR_ENABLE:       return "RADAR_ENABLE";
        case HW_CMD_RADAR_DISABLE:      return "RADAR_DISABLE";
        case HW_CMD_MIC_ENABLE:         return "MIC_ENABLE";
        case HW_CMD_MIC_DISABLE:        return "MIC_DISABLE";
        case HW_CMD_ALARM_TEST:         return "ALARM_TEST";
        case HW_CMD_EXPORT_DATA:        return "EXPORT_DATA";
        case HW_CMD_CLEAR_HISTORY:      return "CLEAR_HISTORY";
        case HW_CMD_NIGHT_MODE_ON:      return "NIGHT_MODE_ON";
        case HW_CMD_NIGHT_MODE_OFF:     return "NIGHT_MODE_OFF";
        case HW_CMD_KEEP_SCREEN_ON:     return "KEEP_SCREEN_ON";
        case HW_CMD_KEEP_SCREEN_OFF:    return "KEEP_SCREEN_OFF";
        case HW_CMD_SYNC_TIME:          return "SYNC_TIME";
        case HW_CMD_RADAR_CALIBRATE:    return "RADAR_CALIBRATE";
        case HW_CMD_MIC_TEST:           return "MIC_TEST";
        case HW_CMD_FACTORY_RESET:      return "FACTORY_RESET";
        default:                        return "UNKNOWN";
    }
}

static void hardware_cmd_execute(hardware_cmd_id_t cmd, int value)
{
    ESP_LOGI(TAG, "Execute: %s (value=%d)", cmd_to_str(cmd), value);

    switch (cmd) {
        case HW_CMD_MONITOR_START:
            /* TODO: 启动雷达监测、开始记录数据 */
            break;

        case HW_CMD_MONITOR_STOP:
            /* TODO: 停止雷达监测、保存报告 */
            break;

        case HW_CMD_DND_ON:
            /* TODO: 关闭蜂鸣器、关闭通知 */
            break;

        case HW_CMD_DND_OFF:
            /* TODO: 恢复通知 */
            break;

        case HW_CMD_WIFI_ON:
#if CONFIG_ENABLE_WIFI
            /* WiFi 已在 wifi_init() 中启动，这里只做状态标记 */
#endif
            ESP_LOGI(TAG, "WiFi ON (not enabled in this build)");
            break;

        case HW_CMD_WIFI_OFF:
#if CONFIG_ENABLE_WIFI
            wifi_disconnect();
#else
            ESP_LOGI(TAG, "WiFi OFF (not enabled)");
#endif
            break;

        case HW_CMD_SET_BRIGHTNESS:
            ESP_LOGI(TAG, "Brightness set to %d%%", value);
            break;

        case HW_CMD_SET_VOLUME:
#if CONFIG_ENABLE_MUSIC_PLAYER
            music_set_volume(value);
#else
            ESP_LOGI(TAG, "Volume set to %d%% (music not enabled)", value);
#endif
            break;

        case HW_CMD_LCD_SCREEN_OFF:
            LCD_BL(0);
            break;

        case HW_CMD_LCD_SCREEN_ON:
            LCD_BL(1);
            break;

        case HW_CMD_RADAR_RESET:
            /* TODO: 复位 R60ABD1 雷达模组 */
            break;

        case HW_CMD_RADAR_ENABLE:
            /* TODO: 启动雷达 */
            break;

        case HW_CMD_RADAR_DISABLE:
            /* TODO: 关闭雷达 */
            break;

        case HW_CMD_MIC_ENABLE:
#if CONFIG_ENABLE_INMP441_AUDIO
            ESP_LOGI(TAG, "INMP441 already active via audio pipeline");
#else
            /* TODO: 启动 INMP441 */
#endif
            break;

        case HW_CMD_MIC_DISABLE:
#if CONFIG_ENABLE_INMP441_AUDIO
            ESP_LOGI(TAG, "INMP441 disable (not implemented)");
#else
            /* TODO: 关闭 INMP441 */
#endif
            break;

        case HW_CMD_ALARM_TEST:
            /* TODO: 蜂鸣器测试 */
            break;

        case HW_CMD_EXPORT_DATA:
            /* TODO: 导出数据到 SD 卡 */
            break;

        case HW_CMD_CLEAR_HISTORY:
            /* TODO: 清空历史数据 */
            break;

        case HW_CMD_NIGHT_MODE_ON:
            /* TODO: 降低屏幕亮度、关闭提示音 */
            break;

        case HW_CMD_NIGHT_MODE_OFF:
            /* TODO: 恢复亮度 */
            break;

        case HW_CMD_KEEP_SCREEN_ON:
            /* TODO: 禁用自动熄屏 */
            break;

        case HW_CMD_KEEP_SCREEN_OFF:
            /* TODO: 启用自动熄屏 */
            break;

        case HW_CMD_SYNC_TIME:
            /* TODO: 网络对时 */
            break;

        case HW_CMD_RADAR_CALIBRATE:
            /* TODO: 雷达空床校准 */
            break;

        case HW_CMD_MIC_TEST:
            /* TODO: 麦克风回路测试 */
            break;

        case HW_CMD_FACTORY_RESET:
            /* TODO: 恢复出厂设置 */
            break;

        default:
            ESP_LOGW(TAG, "Unhandled cmd: %d", cmd);
            break;
    }
}

static void hardware_control_task(void *pvParameters)
{
    (void)pvParameters;
    hardware_cmd_t cmd_msg;

    for (;;) {
        if (xQueueReceive(s_hw_cmd_queue, &cmd_msg, portMAX_DELAY) == pdTRUE) {
            hardware_cmd_execute(cmd_msg.cmd, cmd_msg.value);
        }
    }
}

void hardware_control_init(void)
{
    s_hw_cmd_queue = xQueueCreate(HW_CMD_QUEUE_LEN, sizeof(hardware_cmd_t));
    if (s_hw_cmd_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create hardware cmd queue");
        return;
    }

    xTaskCreate(hardware_control_task, "hw_ctrl_task", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Hardware control init OK");
}

bool hardware_control_send(hardware_cmd_id_t cmd, int value)
{
    if (s_hw_cmd_queue == NULL) {
        ESP_LOGE(TAG, "Queue not initialized");
        return false;
    }

    hardware_cmd_t msg = {
        .cmd = cmd,
        .value = value
    };

    return xQueueSend(s_hw_cmd_queue, &msg, 0) == pdTRUE;
}
