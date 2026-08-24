/**
 * audio_i2s.c — DNESP32S3 板载 ES8388 音频编解码器驱动
 *
 * 硬件：I2C SDA=41 SCL=42 控制 ES8388 (addr=0x10)
 *       I2S MCLK=3 BCLK=46 WS=9 DIN=14 DOUT=10
 *       16kHz 16bit 立体声，选 louder 声道
 *
 * NOTE: ES8388 已禁用 (CONFIG_ENABLE_ES8388_AUDIO=0)
 *       GPIO3/46/9/10/14 已释放给 RGB 屏使用
 *       音频输入改用 INMP441 (inmp441_i2s.c)
 */

#include "audio_i2s.h"
#include "app_config.h"
#include "driver/i2s_std.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"
#include "math.h"

static const char *TAG = "AUDIO_I2S";
static i2s_chan_handle_t s_rx_chan = NULL;

#include "freertos/semphr.h"

#define PCM_RING_SECONDS  4
#define PCM_BUF_SAMPLES   (AUDIO_SAMPLE_RATE * PCM_RING_SECONDS)  /* 64000 */

static int16_t       *s_pcm_buf = NULL;
static SemaphoreHandle_t s_pcm_mutex = NULL;
static int            s_pcm_wr = 0;
static int            s_pcm_filled = 0;
static uint64_t       s_pcm_total_written = 0;

/* 统计 */
static uint32_t s_total_samples = 0;
static uint32_t s_total_zero    = 0;
static uint32_t s_total_clip    = 0;
static float    s_sum_sq_left   = 0;
static float    s_sum_sq_right  = 0;
static int16_t  s_peak          = 0;
static int      s_active_ch     = 0; /* 0=left, 1=right */
static uint32_t s_nochange_ms   = 0;
static int16_t  s_last_val      = 0;
static uint32_t s_last_print_ms = 0;

/* ═══════════════════════════════════════════════════════════════
 * ES8388 I2C 寄存器操作
 * ═══════════════════════════════════════════════════════════════ */

static esp_err_t es_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    esp_err_t ret = i2c_master_write_to_device(ES8388_I2C_PORT, ES8388_I2C_ADDR, buf, 2, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) ESP_LOGE(TAG, "ES8388 write reg=0x%02X fail: %s", reg, esp_err_to_name(ret));
    return ret;
}

/* ES8388 录音初始化 — 对齐 正点原子 BSP + xiaozhi atk-dnesp32s3 */
#if CONFIG_ENABLE_ES8388_AUDIO
static esp_err_t es_init_record(void)
{
    i2c_config_t i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = ES8388_I2C_SDA,
        .scl_io_num = ES8388_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    esp_err_t ret = i2c_param_config(ES8388_I2C_PORT, &i2c_cfg);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "I2C param fail"); return ret; }
    ret = i2c_driver_install(ES8388_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;

    /* Probe */
    uint8_t p = 0;
    ret = i2c_master_write_read_device(ES8388_I2C_PORT, ES8388_I2C_ADDR, &p, 1, &p, 1, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ES8388 NOT FOUND at I2C 0x%02X (SDA=%d SCL=%d)", ES8388_I2C_ADDR, ES8388_I2C_SDA, ES8388_I2C_SCL);
        return ret;
    }
    ESP_LOGI(TAG, "ES8388 detected at I2C 0x%02X", ES8388_I2C_ADDR);

    /* === ES8388 录音初始化 (xiaozhi managed driver register map) === */

    /* 1. 软复位 + 时钟域 */
    es_write(0x00, 0x80); vTaskDelay(pdMS_TO_TICKS(5));
    es_write(0x00, 0x00); vTaskDelay(pdMS_TO_TICKS(20));
    es_write(0x01, 0x58);  /* Clock domain config */
    es_write(0x01, 0x50);

    /* 2. 芯片电源: 全部上电 */
    es_write(0x02, 0x00);  /* CHIPPOWER = 0x00: power up all */

    /* 3. ADC 电源: 完全开启 */
    es_write(0x03, 0x00);  /* ADCPOWER = 0x00: ADC full power */

    /* 4. DAC 电源: 关闭（只录音） */
    es_write(0x04, 0xC0);  /* DACPOWER = 0xC0: disable DAC+Lout+Rout */

    /* 5. 参考电压 + MCLK */
    es_write(0x00, 0x06);  /* Enable reference, 500K driver */
    es_write(0x08, 0x00);  /* MASTERMODE: MCLK/1 */

    /* 6. ADC PGA 增益 +24dB (PGA=8, 3dB/step) */
    es_write(0x09, 0x88);

    /* 7. ADC 输入选择: LINSEL + RINSEL = LIN1/RIN1 (板载mic) */
    es_write(0x0A, 0x00);  /* Input1 (mic) */

    /* 8. ADC 数据格式: left=left ADC, right=left ADC, 16bit I2S */
    es_write(0x0C, 0x4C);

    /* 9. ADC MCLK/sample_rate = 256 */
    es_write(0x0D, 0x02);

    /* 10. ADC 数字音量: 0dB */
    es_write(0x10, 0x00);
    es_write(0x11, 0x00);

    /* 11. DAC 格式 (即使关DAC也要配) */
    es_write(0x17, 0x18);  /* DAC 16bit, I2S */
    es_write(0x18, 0x02);  /* DAC MCLK/fs=256 */
    es_write(0x1A, 0x00);
    es_write(0x1B, 0x00);

    /* 12. 混频器 */
    es_write(0x27, 0xB8);
    es_write(0x2A, 0xB8);
    es_write(0x2B, 0x80);

    /* 13. ADC 使能 */
    es_write(0x21, 0x80);  /* DACCONTROL21: enable ADC path */

    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI(TAG, "[ES8388] init record mode OK (ADC powered, PGA +24dB)");
    return ESP_OK;
}
#endif /* CONFIG_ENABLE_ES8388_AUDIO */

/* ═══════════════════════════════════════════════════════════════
 * I2S 初始化
 * ═══════════════════════════════════════════════════════════════ */

esp_err_t audio_i2s_init(void)
{
#if CONFIG_ENABLE_ES8388_AUDIO
    s_pcm_buf = (int16_t *)heap_caps_malloc(PCM_BUF_SAMPLES * sizeof(int16_t),
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_pcm_buf) { ESP_LOGE(TAG, "PCM buf alloc fail"); return ESP_ERR_NO_MEM; }

    s_pcm_mutex = xSemaphoreCreateMutex();
    if (!s_pcm_mutex) {
        ESP_LOGE(TAG, "PCM mutex create fail");
        return ESP_ERR_NO_MEM;
    }

    memset(s_pcm_buf, 0, PCM_BUF_SAMPLES * sizeof(int16_t));
    s_pcm_wr = 0;
    s_pcm_filled = 0;
    s_pcm_total_written = 0;

    esp_err_t ret = es_init_record();
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "Init I2S0: MCLK=%d BCLK=%d WS=%d DIN=%d DOUT=%d rate=%d",
             AUDIO_I2S_MCLK, AUDIO_I2S_BCLK, AUDIO_I2S_WS, AUDIO_I2S_DIN, AUDIO_I2S_DOUT, AUDIO_SAMPLE_RATE);

    i2s_chan_config_t chan_cfg = {
        .id = AUDIO_I2S_NUM,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 4,
        .dma_frame_num = 512,
        .auto_clear_after_cb = true,
    };
    ret = i2s_new_channel(&chan_cfg, NULL, &s_rx_chan);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "i2s_new_channel fail"); return ret; }

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = AUDIO_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,   /* 256×fs = 4.096MHz MCLK */
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,         /* 立体声 → 选 louder 声道 */
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
#if defined(I2S_HW_VERSION_2)
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
#endif
        },
        .gpio_cfg = {
            .mclk = AUDIO_I2S_MCLK,
            .bclk = AUDIO_I2S_BCLK,
            .ws   = AUDIO_I2S_WS,
            .dout = AUDIO_I2S_DOUT,
            .din  = AUDIO_I2S_DIN,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };

    ret = i2s_channel_init_std_mode(s_rx_chan, &std_cfg);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "i2s_init_std fail"); return ret; }
    ret = i2s_channel_enable(s_rx_chan);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "i2s_enable fail"); return ret; }

    ESP_LOGI(TAG, "I2S stereo recording started, 16kHz 16bit");
    return ESP_OK;
#else
    (void)s_rx_chan;
    ESP_LOGI(TAG, "ES8388 disabled — using INMP441 instead");
    return ESP_OK;
#endif /* CONFIG_ENABLE_ES8388_AUDIO */
}

void audio_i2s_deinit(void)
{
    if (s_rx_chan) { i2s_channel_disable(s_rx_chan); i2s_del_channel(s_rx_chan); s_rx_chan = NULL; }
}

/* ── Ring buffer 写入（capture task 调用，持续更新最近 4s PCM）── */
static void audio_i2s_ring_write(const int16_t *src, int n)
{
    if (!s_pcm_buf || !src || n <= 0) return;
    if (n > PCM_BUF_SAMPLES) { src += (n - PCM_BUF_SAMPLES); n = PCM_BUF_SAMPLES; }

    if (s_pcm_mutex) xSemaphoreTake(s_pcm_mutex, portMAX_DELAY);

    int first = PCM_BUF_SAMPLES - s_pcm_wr;
    if (first > n) first = n;
    memcpy(s_pcm_buf + s_pcm_wr, src, first * sizeof(int16_t));
    if (n > first) memcpy(s_pcm_buf, src + first, (n - first) * sizeof(int16_t));

    s_pcm_wr += n;
    if (s_pcm_wr >= PCM_BUF_SAMPLES) s_pcm_wr %= PCM_BUF_SAMPLES;
    s_pcm_filled += n;
    if (s_pcm_filled > PCM_BUF_SAMPLES) s_pcm_filled = PCM_BUF_SAMPLES;
    s_pcm_total_written += (uint64_t)n;

    if (s_pcm_mutex) xSemaphoreGive(s_pcm_mutex);
}

/**
 * @brief 返回最近 N 个连续 PCM 样本，时间顺序：buf[0]=最旧, buf[n-1]=最新。
 */
int audio_i2s_get_recent_pcm(int16_t *buf, int samples)
{
    if (!buf || samples <= 0 || !s_pcm_buf) return 0;
    if (samples > PCM_BUF_SAMPLES) samples = PCM_BUF_SAMPLES;

    int wr = 0, filled = 0, n = samples;
    if (s_pcm_mutex) {
        if (xSemaphoreTake(s_pcm_mutex, pdMS_TO_TICKS(30)) != pdTRUE) return 0;
    }
    wr = s_pcm_wr;
    filled = s_pcm_filled;
    if (n > filled) n = filled;
    if (n <= 0) { if (s_pcm_mutex) xSemaphoreGive(s_pcm_mutex); return 0; }

    int start = wr - n;
    while (start < 0) start += PCM_BUF_SAMPLES;
    int first = PCM_BUF_SAMPLES - start;
    if (first > n) first = n;
    memcpy(buf, s_pcm_buf + start, first * sizeof(int16_t));
    if (n > first) memcpy(buf + first, s_pcm_buf, (n - first) * sizeof(int16_t));

    if (s_pcm_mutex) xSemaphoreGive(s_pcm_mutex);
    return n;
}

int audio_i2s_get_1sec_pcm(int16_t *buf, int max_samples)
{
    if (!buf || max_samples <= 0) return 0;
    int want = AUDIO_SAMPLE_RATE;
    if (want > max_samples) want = max_samples;
    return audio_i2s_get_recent_pcm(buf, want);
}

void audio_i2s_get_ring_info(audio_i2s_ring_info_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (s_pcm_mutex) {
        if (xSemaphoreTake(s_pcm_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
    }
    out->write_pos = s_pcm_wr;
    out->filled_samples = s_pcm_filled;
    out->filled_ms = (s_pcm_filled * 1000) / AUDIO_SAMPLE_RATE;
    out->total_written = s_pcm_total_written;
    if (s_pcm_mutex) xSemaphoreGive(s_pcm_mutex);
}

void audio_i2s_get_stats(audio_i2s_stats_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(audio_i2s_stats_t));
    out->samples_count = s_total_samples;
    out->peak = s_peak;
    out->zero_count = s_total_zero;
    out->clipped_count = s_total_clip;
    int half = s_total_samples / 2;
    out->left_rms  = half > 0 ? sqrtf(s_sum_sq_left  / half) : 0;
    out->right_rms = half > 0 ? sqrtf(s_sum_sq_right / half) : 0;
    out->mic_ok = (s_total_samples > 32000) && (s_peak > 20);
    out->active_channel = s_active_ch;
}

/* ═══════════════════════════════════════════════════════════════
 * 采集任务（立体声 → 选 louder 声道 → mono ring buffer）
 * ═══════════════════════════════════════════════════════════════ */

#if CONFIG_ENABLE_ES8388_AUDIO
void audio_capture_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Capture task started (stereo→mono, auto channel select)");

    /* 静态缓冲区放 PSRAM — stereo 256 frames max */
    int16_t *raw = (int16_t *)heap_caps_malloc(512 * sizeof(int16_t),
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!raw) { ESP_LOGE(TAG, "Capture buf alloc fail"); vTaskDelete(NULL); return; }

    while (1) {
        size_t bytes = 0;
        esp_err_t ret = i2s_channel_read(s_rx_chan, raw, 512 * sizeof(int16_t), &bytes, pdMS_TO_TICKS(100));
        if (ret != ESP_OK) {
            if (ret != ESP_ERR_TIMEOUT) ESP_LOGW(TAG, "i2s_read err: %s", esp_err_to_name(ret));
            continue;
        }

        int total_i16 = bytes / sizeof(int16_t);
        int stereo_frames = total_i16 / 2;
        int mono_count = (stereo_frames > PCM_BUF_SAMPLES) ? PCM_BUF_SAMPLES : stereo_frames;

        /* mono 临时缓冲，栈上 256×2=512B 安全 */
        int16_t mono_buf[256];
        if (mono_count > 256) mono_count = 256;

        for (int i = 0; i < mono_count; i++) {
            int16_t left  = raw[i * 2];
            int16_t right = raw[i * 2 + 1];

            s_sum_sq_left  += (float)left  * left;
            s_sum_sq_right += (float)right * right;
            s_total_samples += 2;

            int16_t pcm = (s_active_ch == 0) ? left : right;
            mono_buf[i] = pcm;

            if (pcm > s_peak) s_peak = pcm;
            else if (-pcm > s_peak) s_peak = -pcm;
            if (pcm == 0) s_total_zero++;
            if (pcm == 32767 || pcm == -32768) s_total_clip++;
            if (pcm == s_last_val) s_nochange_ms++;
            s_last_val = pcm;
        }

        /* 批量写入 ring buffer */
        audio_i2s_ring_write(mono_buf, mono_count);

        /* 每 5 秒更新声道选择 + 打印，减少串口刷屏 */
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (now - s_last_print_ms >= 5000) {
            s_last_print_ms = now;

            int half = s_total_samples / 2;
            float lrms = half > 0 ? sqrtf(s_sum_sq_left  / half) : 0;
            float rrms = half > 0 ? sqrtf(s_sum_sq_right / half) : 0;

            /* 自动选 louder 声道 */
            if (lrms > rrms * 1.2f)      s_active_ch = 0;
            else if (rrms > lrms * 1.2f) s_active_ch = 1;

            bool both_low = (lrms < 5 && rrms < 5);
            if (both_low) ESP_LOGE(TAG, "MIC ERROR: left=%.1f right=%.1f — check ES8388 init/I2C addr/I2S pins", lrms, rrms);
            else if (s_nochange_ms > s_total_samples * 0.8f) ESP_LOGW(TAG, "MIC WARN: signal not changing");

            ESP_LOGI(TAG, "samples=%lu left=%.1f right=%.1f peak=%d ch=%s %s",
                     s_total_samples, lrms, rrms, s_peak,
                     s_active_ch == 0 ? "L" : "R",
                     both_low ? "⚠ MIC_ERROR" : "✓");

            /* 重置每秒统计 */
            s_total_samples = 0; s_total_zero = 0; s_total_clip = 0;
            s_sum_sq_left = 0; s_sum_sq_right = 0;
            s_peak = 0; s_nochange_ms = 0;
        }
    }
}
#else
void audio_capture_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGW(TAG, "ES8388 disabled — audio_capture_task not started");
    vTaskDelete(NULL);
}
#endif /* CONFIG_ENABLE_ES8388_AUDIO */
