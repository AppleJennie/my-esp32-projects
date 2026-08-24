/**
 * ESP32-S3 内置温度传感器驱动实现
 */
#include "chip_temp.h"
#include <driver/temperature_sensor.h>
#include <esp_log.h>
#include <esp_err.h>

static const char *TAG = "CHIP_TEMP";

static temperature_sensor_handle_t s_temp_handle = NULL;
static bool s_initialized = false;

int chip_temp_init(void)
{
    if (s_initialized) return 0;

    temperature_sensor_config_t cfg = {
        .range_min = 20,
        .range_max = 50,
    };

    esp_err_t ret = temperature_sensor_install(&cfg, &s_temp_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Install failed: %s", esp_err_to_name(ret));
        return -1;
    }

    ret = temperature_sensor_enable(s_temp_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Enable failed: %s", esp_err_to_name(ret));
        return -1;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Chip temp sensor init OK");
    return 0;
}

short chip_temp_read(void)
{
    return (short)chip_temp_read_float();
}

float chip_temp_read_float(void)
{
    if (!s_initialized || !s_temp_handle) return -999.0f;

    float temp = 0.0f;
    esp_err_t ret = temperature_sensor_get_celsius(s_temp_handle, &temp);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Read failed: %s", esp_err_to_name(ret));
        return -999.0f;
    }
    return temp;
}
