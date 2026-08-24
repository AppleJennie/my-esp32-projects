/**
 * TEMT6000 环境光传感器实现（ADC1_CH3, GPIO4）
 */
#include "temt6000.h"
#include "app_role_config.h"
#include <driver/adc.h>
#include <driver/gpio.h>
#include <esp_log.h>

static const char *TAG = "TEMT6000";
static bool s_ready = false;

esp_err_t temt6000_init(void)
{
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(TEMT6000_ADC_CHANNEL, TEMT6000_ADC_ATTEN);
    s_ready = true;
    ESP_LOGI(TAG, "Init OK GPIO=%d ADC1_CH%d", TEMT6000_ADC_GPIO, TEMT6000_ADC_CHANNEL);
    return ESP_OK;
}

int temt6000_read_raw(void)
{
    if (!s_ready) return -1;
    return adc1_get_raw(TEMT6000_ADC_CHANNEL);
}

float temt6000_read_lux(void)
{
    int raw = temt6000_read_raw();
    if (raw < 0) return -1.0f;
    /* TEMT6000: Iout = raw/4095 * 3.3V / 10kΩ, 1μA ≈ 2lux 粗略估计 */
    float voltage = (float)raw * 3.3f / 4095.0f;
    float current_ua = voltage / 10000.0f * 1000000.0f;
    return current_ua * 2.0f;
}
