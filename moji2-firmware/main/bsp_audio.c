#include "bsp_audio.h"

#include <math.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"

static const char *TAG = "audio";

static i2c_master_dev_handle_t s_es8311 = NULL;
static i2s_chan_handle_t s_tx = NULL;
static i2s_chan_handle_t s_rx = NULL;

/* ------------------------------------------------------------------ */
/* ES8311 寄存器配置（标准初始化序列，参考 esp-adf es8311 驱动默认值） */
/* ------------------------------------------------------------------ */

static esp_err_t es8311_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_es8311, buf, sizeof(buf), 100);
}

static esp_err_t es8311_init(void)
{
    /* {寄存器, 值}，顺序执行 */
    static const uint8_t init_seq[][2] = {
        {0x45, 0x00},   /* GPIO 引脚全部用作音频功能 */
        {0x01, 0x30},   /* 时钟管理1：选择 MCLK/BCLK 输入源 */
        {0x02, 0x00},   /* 时钟管理2：标准速度模式，MCLK 不分频 */
        {0x03, 0x10},   /* 时钟管理3：使能 ADC 时钟与模拟 */
        {0x04, 0x10},   /* 时钟管理4：使能 DAC 时钟与模拟 */
        {0x05, 0x00},   /* 时钟管理5：正常工作模式 */
        {0x0B, 0x00},   /* 系统：内部基准正常 */
        {0x0C, 0x00},   /* 系统：内部基准正常 */
        {0x10, 0x1F},   /* 系统：ADC 模拟上电 */
        {0x11, 0x7F},   /* 系统：DAC 模拟上电 */
        {0x00, 0x80},   /* 复位/模式：芯片使能，I2S 从模式 (bit6=0) */
        {0x01, 0x3F},   /* 时钟管理1：打开全部内部时钟 */
        {0x09, 0x0C},   /* ADC SDP：I2S 格式，16bit */
        {0x0A, 0x0C},   /* DAC SDP：I2S 格式，16bit */
        {0x0E, 0x02},   /* 系统：PPR 保护阈值 */
        {0x12, 0x00},   /* 系统：DAC 正常输出 */
        {0x14, 0x1A},   /* ADC 输入：MIC1 差分输入 */
        {0x0D, 0x01},   /* 系统：使能模拟参考电路 */
        {0x15, 0x40},   /* ADC：增益爬坡速率 */
        {0x1B, 0x0A},   /* ADC：高通滤波器 */
        {0x1C, 0x6A},   /* ADC：ALC/EQ 配置 */
        {0x37, 0x48},   /* DAC：软启动、去加重 */
        {0x44, 0x08},   /* GPIO：DAC 输出通路使能 */
        {0x13, 0x10},   /* 系统：ADC->DAC 数据通路使能 */
        {0x17, 0xC8},   /* ADC 数字音量/PGA 增益（麦克风灵敏度，可调） */
        {0x32, 0xBF},   /* DAC 数字音量：约 0dB */
        {0x16, 0x24},   /* ADC：数字通路使能 */
    };

    for (size_t i = 0; i < sizeof(init_seq) / sizeof(init_seq[0]); i++) {
        ESP_RETURN_ON_ERROR(es8311_write(init_seq[i][0], init_seq[i][1]),
                            TAG, "ES8311 写寄存器 0x%02X 失败", init_seq[i][0]);
    }
    /* 等待模拟电路稳定 */
    vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

/* ------------------------------------------------------------------ */

static esp_err_t i2c_init(void)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_PORT_NUM,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = { .enable_internal_pullup = true },
    };
    i2c_master_bus_handle_t bus = NULL;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &bus), TAG, "i2c bus init failed");

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ES8311_I2C_ADDR,
        .scl_speed_hz = 400000,
    };
    return i2c_master_bus_add_device(bus, &dev_cfg, &s_es8311);
}

static esp_err_t i2s_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx, &s_rx), TAG, "i2s new channel failed");

    /* 标准飞利浦格式，16bit 单声道，MCLK = 256 x Fs */
    const i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = PIN_I2S_MCLK,
            .bclk = PIN_I2S_SCLK,
            .ws   = PIN_I2S_LRCK,
            .dout = PIN_I2S_DOUT,
            .din  = PIN_I2S_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std_cfg), TAG, "i2s tx init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx, &std_cfg), TAG, "i2s rx init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "i2s tx enable failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx), TAG, "i2s rx enable failed");
    return ESP_OK;
}

esp_err_t bsp_audio_init(void)
{
    /* 功放控制脚：默认关闭 */
    const gpio_config_t pa_cfg = {
        .pin_bit_mask = 1ULL << PIN_PA_CTRL,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&pa_cfg), TAG, "pa gpio failed");
    gpio_set_level(PIN_PA_CTRL, 0);

    ESP_RETURN_ON_ERROR(i2c_init(), TAG, "i2c init failed");
    ESP_RETURN_ON_ERROR(es8311_init(), TAG, "es8311 init failed");
    ESP_RETURN_ON_ERROR(i2s_init(), TAG, "i2s init failed");

    ESP_LOGI(TAG, "音频初始化完成 (ES8311 @ 0x%02X, %d Hz)", ES8311_I2C_ADDR, AUDIO_SAMPLE_RATE);
    return ESP_OK;
}

void bsp_audio_pa_enable(bool enable)
{
    gpio_set_level(PIN_PA_CTRL, enable ? 1 : 0);
}

esp_err_t bsp_audio_play_tone(int freq_hz, int ms, float vol)
{
    if (freq_hz <= 0 || ms <= 0) return ESP_ERR_INVALID_ARG;
    if (vol > 1.0f) vol = 1.0f;

    size_t samples = AUDIO_SAMPLE_RATE * ms / 1000;
    int16_t *pcm = malloc(samples * sizeof(int16_t));
    ESP_RETURN_ON_FALSE(pcm != NULL, ESP_ERR_NO_MEM, TAG, "no mem");

    for (size_t i = 0; i < samples; i++) {
        /* 首尾 5ms 淡入淡出，避免爆音 */
        float env = 1.0f;
        size_t fade = AUDIO_SAMPLE_RATE / 200;
        if (i < fade) env = (float)i / fade;
        else if (i > samples - fade) env = (float)(samples - i) / fade;
        pcm[i] = (int16_t)(sinf(2.0f * (float)M_PI * freq_hz * i / AUDIO_SAMPLE_RATE)
                           * 32767.0f * vol * env);
    }

    bsp_audio_pa_enable(true);
    size_t written = 0;
    esp_err_t ret = i2s_channel_write(s_tx, pcm, samples * sizeof(int16_t), &written,
                                      ms + 500);
    /* 等 FIFO 播完再关功放 */
    vTaskDelay(pdMS_TO_TICKS(ms + 30));
    bsp_audio_pa_enable(false);
    free(pcm);
    return ret;
}

void bsp_audio_play_boot_sound(void)
{
    bsp_audio_play_tone(880, 120, 0.25f);
    vTaskDelay(pdMS_TO_TICKS(60));
    bsp_audio_play_tone(1320, 160, 0.25f);
}

int bsp_audio_read_mic(int16_t *buf, size_t max_samples)
{
    size_t read = 0;
    esp_err_t ret = i2s_channel_read(s_rx, buf, max_samples * sizeof(int16_t), &read,
                                     1000);
    if (ret != ESP_OK) {
        return -1;
    }
    return (int)(read / sizeof(int16_t));
}
