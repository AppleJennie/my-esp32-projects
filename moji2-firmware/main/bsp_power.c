#include "bsp_power.h"

#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "board.h"

static const char *TAG = "power";

static adc_oneshot_unit_handle_t s_adc = NULL;
static adc_cali_handle_t s_cali = NULL;
static bool s_cali_ok = false;

esp_err_t bsp_power_init(void)
{
    const adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc));

    const adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,      /* 满量程约 3.1V，分压后 4.2V/2 = 2.1V */
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, BAT_ADC_CHANNEL, &chan_cfg));

    /* 尝试启用曲线拟合校准；不支持则退回原始值线性换算 */
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) == ESP_OK) {
        s_cali_ok = true;
    } else {
        ESP_LOGW(TAG, "ADC 校准不可用，使用近似换算");
    }
    return ESP_OK;
}

float bsp_power_battery_voltage(void)
{
    int raw = 0;
    if (adc_oneshot_read(s_adc, BAT_ADC_CHANNEL, &raw) != ESP_OK) {
        return -1.0f;
    }

    int mv = 0;
    if (s_cali_ok && adc_cali_raw_to_voltage(s_cali, raw, &mv) == ESP_OK) {
        /* mv 已是分压点电压 */
    } else {
        mv = raw * 3100 / 4095;     /* 粗略换算：DB_12 满量程约 3.1V @ 12bit */
    }
    return mv * BAT_DIVIDER_RATIO / 1000.0f;
}

int bsp_power_battery_percent(void)
{
    float v = bsp_power_battery_voltage();
    if (v < 0) return -1;

    int pct = (int)((v - 3.3f) / (4.2f - 3.3f) * 100.0f);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}
