/**
 * inmp441_i2s.c — INMP441 MEMS 麦克风 I2S 驱动 v3
 *
 * 32-bit slot 接收 → raw32 诊断 → 多 shift 对比 → PCM16 ring buffer
 *
 * INMP441 24-bit 数据在 32-bit slot 中左对齐（MSB 在 slot bit 31）。
 * 右移 INMP441_PCM_SHIFT 位得到 16-bit 有符号 PCM。
 */

#include "inmp441_i2s.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "string.h"
#include "math.h"

static const char *TAG = "INMP441";

/* ── I2S 通道 ── */
static i2s_chan_handle_t s_rx_chan = NULL;

/* ── Ring Buffer (16-bit PCM after shift) ── */
static int16_t          *s_pcm_buf = NULL;
static SemaphoreHandle_t s_pcm_mutex = NULL;
static int               s_pcm_wr = 0;
static int               s_pcm_filled = 0;
static uint64_t          s_pcm_total = 0;

/* ── 统计 ── */
static float     s_cur_rms = 0.0f;
static int16_t   s_cur_peak = 0;
static uint32_t  s_total_samples = 0;
static bool      s_mic_ok = false;

/* ── raw32 诊断 ── */
static int32_t  s_raw32_min  = INT32_MAX;
static int32_t  s_raw32_max  = INT32_MIN;
static float    s_raw32_avg  = 0.0f;
static int32_t  s_raw32_peak = 0;
static int16_t  s_pcm16_min  = INT16_MAX;
static int16_t  s_pcm16_max  = INT16_MIN;

/* ── 多 shift 对比 ── */
static float   s_shift8_rms  = 0.0f;
static int32_t s_shift8_peak = 0;
static float   s_shift14_rms = 0.0f;
static int32_t s_shift14_peak = 0;
static float   s_shift16_rms = 0.0f;
static int32_t s_shift16_peak = 0;

/* ── 拍手检测峰值历史 ── */
static int32_t s_peak_history_3 = 0;
static int32_t s_peak_history_2 = 0;
static int32_t s_peak_history_1 = 0;

/* ── 声道自适应 ── */
static int s_current_slot_left = 1;  /* 0=RIGHT, 1=LEFT */
static int s_slot_stable_count = 0;

/* ═══════════════════════════════════════════════════════════════
 * PCM 右移
 * ═══════════════════════════════════════════════════════════════ */
static inline int16_t pcm_shift(int32_t raw32)
{
#if INMP441_PCM_SHIFT > 0
    return (int16_t)(raw32 >> INMP441_PCM_SHIFT);
#else
    return (int16_t)(raw32 >> 14);
#endif
}

/* ── 一阶 DC blocker（安静时 PCM 收敛到零附近）── */
static float s_dc_est = 0.0f;
static inline int16_t dc_remove_sample(int16_t x)
{
    s_dc_est = 0.995f * s_dc_est + 0.005f * (float)x;
    int32_t y = (int32_t)((float)x - s_dc_est);
    if (y > 32767)  y = 32767;
    if (y < -32768) y = -32768;
    return (int16_t)y;
}

/* ═══════════════════════════════════════════════════════════════
 * Ring Buffer
 * ═══════════════════════════════════════════════════════════════ */
static void ring_write(const int16_t *src, int n)
{
    if (!s_pcm_buf || !src || n <= 0) return;
    if (n > INMP441_RING_SAMPLES) { src += (n - INMP441_RING_SAMPLES); n = INMP441_RING_SAMPLES; }
    if (s_pcm_mutex) xSemaphoreTake(s_pcm_mutex, portMAX_DELAY);
    int first = INMP441_RING_SAMPLES - s_pcm_wr;
    if (first > n) first = n;
    memcpy(s_pcm_buf + s_pcm_wr, src, first * sizeof(int16_t));
    if (n > first) memcpy(s_pcm_buf, src + first, (n - first) * sizeof(int16_t));
    s_pcm_wr = (s_pcm_wr + n) % INMP441_RING_SAMPLES;
    s_pcm_filled += n;
    if (s_pcm_filled > INMP441_RING_SAMPLES) s_pcm_filled = INMP441_RING_SAMPLES;
    s_pcm_total += (uint64_t)n;
    if (s_pcm_mutex) xSemaphoreGive(s_pcm_mutex);
}

static int ring_read_recent(int16_t *buf, int samples)
{
    if (!buf || samples <= 0 || !s_pcm_buf) return 0;
    if (samples > INMP441_RING_SAMPLES) samples = INMP441_RING_SAMPLES;
    int wr, filled, n = samples;
    if (s_pcm_mutex) {
        if (xSemaphoreTake(s_pcm_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    }
    wr = s_pcm_wr; filled = s_pcm_filled;
    if (n > filled) n = filled;
    if (n <= 0) { if (s_pcm_mutex) xSemaphoreGive(s_pcm_mutex); return 0; }
    int start = wr - n; while (start < 0) start += INMP441_RING_SAMPLES;
    int first = INMP441_RING_SAMPLES - start;
    if (first > n) first = n;
    memcpy(buf, s_pcm_buf + start, first * sizeof(int16_t));
    if (n > first) memcpy(buf + first, s_pcm_buf, (n - first) * sizeof(int16_t));
    if (s_pcm_mutex) xSemaphoreGive(s_pcm_mutex);
    return n;
}

/* ═══════════════════════════════════════════════════════════════
 * I2S 初始化 — 32-bit slot
 * ═══════════════════════════════════════════════════════════════ */
esp_err_t inmp441_i2s_init(void)
{
    s_pcm_buf = (int16_t *)heap_caps_malloc(
        INMP441_RING_SAMPLES * sizeof(int16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_pcm_buf) { ESP_LOGE(TAG, "ring buf alloc FAIL"); return ESP_ERR_NO_MEM; }
    memset(s_pcm_buf, 0, INMP441_RING_SAMPLES * sizeof(int16_t));
    s_pcm_wr = s_pcm_filled = 0; s_pcm_total = 0;

    s_pcm_mutex = xSemaphoreCreateMutex();
    if (!s_pcm_mutex) { heap_caps_free(s_pcm_buf); s_pcm_buf = NULL; return ESP_ERR_NO_MEM; }

    i2s_chan_config_t chan_cfg = {
        .id = INMP441_I2S_NUM,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 4,
        .dma_frame_num = 256,
        .auto_clear = true,
    };
    esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, &s_rx_chan);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "i2s_new_channel fail: %s", esp_err_to_name(ret)); goto fail; }

    /* ── 32-bit Philips I2S Standard ── */
    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = INMP441_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_PLL_160M,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            INMP441_DATA_BIT_WIDTH,
            I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = INMP441_I2S_BCLK,
            .ws   = INMP441_I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din  = INMP441_I2S_DIN,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };

    /* slot_bit_width */
    std_cfg.slot_cfg.slot_bit_width = INMP441_SLOT_BIT_WIDTH;

    /* slot 选择 */
#if INMP441_USE_LEFT_SLOT
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    s_current_slot_left = 1;
#else
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
    s_current_slot_left = 0;
#endif

    ret = i2s_channel_init_std_mode(s_rx_chan, &std_cfg);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "i2s_channel_init_std_mode fail: %s", esp_err_to_name(ret)); goto fail_chan; }

    ESP_LOGI(TAG, "I2S init OK: BCLK=%d WS=%d DIN=%d SR=%d Hz "
             "data_bit=32 slot_bit=AUTO slot=%s shift=%d (PLL_160M)",
             INMP441_I2S_BCLK, INMP441_I2S_WS, INMP441_I2S_DIN,
             INMP441_SAMPLE_RATE,
             s_current_slot_left ? "LEFT" : "RIGHT",
             INMP441_PCM_SHIFT);
    return ESP_OK;

fail_chan:
    i2s_del_channel(s_rx_chan); s_rx_chan = NULL;
fail:
    if (s_pcm_mutex) { vSemaphoreDelete(s_pcm_mutex); s_pcm_mutex = NULL; }
    if (s_pcm_buf)   { heap_caps_free(s_pcm_buf);    s_pcm_buf = NULL; }
    return ret;
}

/* ═══════════════════════════════════════════════════════════════
 * Capture Task
 * ═══════════════════════════════════════════════════════════════ */
esp_err_t inmp441_audio_start(void)
{
    if (!s_rx_chan) { ESP_LOGE(TAG, "I2S not init"); return ESP_ERR_INVALID_STATE; }
    esp_err_t ret = i2s_channel_enable(s_rx_chan);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "i2s_channel_enable fail: %s", esp_err_to_name(ret)); return ret; }
    BaseType_t ok = xTaskCreatePinnedToCore(inmp441_capture_task, "inmp441_cap",
                                             8192, NULL, 9, NULL, 0);
    if (ok != pdPASS) { ESP_LOGE(TAG, "task create FAIL"); return ESP_FAIL; }
    ESP_LOGI(TAG, "Capture task started CPU0");
    return ESP_OK;
}

void inmp441_capture_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Capture task running CPU%d slot=%s shift=%d",
             xPortGetCoreID(),
             s_current_slot_left ? "LEFT" : "RIGHT",
             INMP441_PCM_SHIFT);

    /* 32-bit capture block */
    int32_t cap_buf[INMP441_CAPTURE_BLOCK];
    int16_t pcm_buf[INMP441_CAPTURE_BLOCK];
    uint32_t last_diag_ms = 0;
    int      slot_dead_count = 0;
    #define SLOT_SWITCH_DEAD_THRESH 20   /* LEFT 连续 20 次无效 → 切 RIGHT 测试 */

    while (1) {
        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(s_rx_chan, cap_buf,
                                          INMP441_CAPTURE_BLOCK * sizeof(int32_t),
                                          &bytes_read, pdMS_TO_TICKS(200));

        if (ret == ESP_OK && bytes_read > 0) {
            int samples = bytes_read / sizeof(int32_t);

            /* ── raw32 统计 ── */
            int32_t raw_min32 = INT32_MAX, raw_max32 = INT32_MIN, raw_peak32 = 0;
            int64_t raw_sum64 = 0;
            for (int i = 0; i < samples; i++) {
                int32_t v = cap_buf[i];
                raw_sum64 += v;
                if (v < raw_min32) raw_min32 = v;
                if (v > raw_max32) raw_max32 = v;
                int32_t absv = (v >= 0) ? v : -v;
                if (absv > raw_peak32) raw_peak32 = absv;
            }
            s_raw32_min  = raw_min32;
            s_raw32_max  = raw_max32;
            s_raw32_avg  = (float)((double)raw_sum64 / (double)samples);
            s_raw32_peak = raw_peak32;

            /* ── 多 shift 对比 ── */
            float sum_sq_8 = 0, sum_sq_14 = 0, sum_sq_16 = 0;
            int32_t pk_8 = 0, pk_14 = 0, pk_16 = 0;
            int16_t pmin16 = INT16_MAX, pmax16 = INT16_MIN;

            /* ── PCM shift + 写入 ── */
            int16_t peak16 = 0;
            float sum_sq16 = 0.0f;
            for (int i = 0; i < samples; i++) {
                int32_t r = cap_buf[i];

                /* 三档 shift */
                int32_t s8  = r >> 8;
                int32_t s14 = r >> 14;
                int32_t s16 = r >> 16;
                sum_sq_8  += (float)s8  * (float)s8;
                sum_sq_14 += (float)s14 * (float)s14;
                sum_sq_16 += (float)s16 * (float)s16;
                int32_t a8 = (s8 >=0) ? s8 : -s8;
                int32_t a14= (s14>=0) ? s14 : -s14;
                int32_t a16= (s16>=0) ? s16 : -s16;
                if (a8  > pk_8)  pk_8  = a8;
                if (a14 > pk_14) pk_14 = a14;
                if (a16 > pk_16) pk_16 = a16;

                /* 主 shift + DC 去偏置 */
                int16_t v = pcm_shift(r);
                v = dc_remove_sample(v);
                pcm_buf[i] = v;
                if (v > peak16) peak16 = v;
                else if (-v > peak16) peak16 = -v;
                if (v < pmin16) pmin16 = v;
                if (v > pmax16) pmax16 = v;
                sum_sq16 += (float)v * (float)v;
            }

            s_shift8_rms   = sqrtf(sum_sq_8  / (float)samples);
            s_shift8_peak  = pk_8;
            s_shift14_rms  = sqrtf(sum_sq_14 / (float)samples);
            s_shift14_peak = pk_14;
            s_shift16_rms  = sqrtf(sum_sq_16 / (float)samples);
            s_shift16_peak = pk_16;

            s_pcm16_min = pmin16;
            s_pcm16_max = pmax16;

            ring_write(pcm_buf, samples);

            s_cur_rms  = sqrtf(sum_sq16 / (float)samples);
            s_cur_peak = peak16;
            s_total_samples += (uint32_t)samples;

            /* ── 峰值历史 ── */
            s_peak_history_3 = s_peak_history_2;
            s_peak_history_2 = s_peak_history_1;
            s_peak_history_1 = raw_peak32;

            /* ── 麦克风有效性判断 ── */
            /* raw32_peak 有变化 + pcm16_peak > 20 */
            bool raw_varying = (raw_peak32 > 1000) ||
                (s_peak_history_3 > 0 && s_peak_history_2 > 0 && s_peak_history_1 > 0 &&
                 (labs(s_peak_history_1 - s_peak_history_3) > 500 ||
                  labs(s_peak_history_2 - s_peak_history_3) > 500));
            bool pcm_ok = (peak16 > 20);
            if (s_total_samples > INMP441_SAMPLE_RATE * 3) {
                s_mic_ok = raw_varying && pcm_ok;
            }

            /* ── 声道自动切换测试 ── */
            /* 如果 LEFT 连续无效，提示切 RIGHT 试试 */
            if (!s_mic_ok && s_total_samples > INMP441_SAMPLE_RATE * 5) {
                slot_dead_count++;
                if (slot_dead_count == SLOT_SWITCH_DEAD_THRESH) {
                    ESP_LOGW(TAG,
                        "*** LEFT slot dead after %lu samples! "
                        "Try L/R=3.3V (RIGHT slot) or set INMP441_USE_LEFT_SLOT=0 ***",
                        (unsigned long)s_total_samples);
                }
            } else {
                slot_dead_count = 0;
            }
        } else if (ret != ESP_OK && ret != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "i2s_channel_read err: %s (0x%x)", esp_err_to_name(ret), (int)ret);
        }

        /* ── 每 5 秒诊断 ── */
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (now - last_diag_ms >= 5000) {
            last_diag_ms = now;
            int filled_ms = (s_pcm_filled * 1000) / INMP441_SAMPLE_RATE;

            ESP_LOGI(TAG,
                "[INMP441_RAW] raw32_min=%ld raw32_max=%ld raw32_avg=%.0f "
                "raw32_peak=%ld "
                "pcm16_min=%d pcm16_max=%d "
                "rms=%.1f peak=%d "
                "slot=%s shift=%d",
                (long)s_raw32_min, (long)s_raw32_max, s_raw32_avg,
                (long)s_raw32_peak,
                s_pcm16_min, s_pcm16_max,
                s_cur_rms, s_cur_peak,
                s_current_slot_left ? "LEFT" : "RIGHT",
                INMP441_PCM_SHIFT);

#if INMP441_DIAG_MULTI_SHIFT
            ESP_LOGI(TAG,
                "[INMP441_SHIFT] "
                "s8: rms=%.1f peak=%ld  "
                "s14: rms=%.1f peak=%ld  "
                "s16: rms=%.1f peak=%ld",
                s_shift8_rms,  (long)s_shift8_peak,
                s_shift14_rms, (long)s_shift14_peak,
                s_shift16_rms, (long)s_shift16_peak);
#endif

            ESP_LOGI(TAG,
                "capture: filled=%dms (%lu total) mic=%s "
                "raw_var=%d pcm_ok=%d peak_hist=%ld/%ld/%ld hwm=%lu",
                filled_ms, (unsigned long)s_pcm_total,
                s_mic_ok ? "OK" : s_cur_peak <= 1 ? "INVALID" : "LOW",
                (s_raw32_peak > 1000) ? 1 : 0,
                (s_cur_peak > 20) ? 1 : 0,
                (long)s_peak_history_1, (long)s_peak_history_2, (long)s_peak_history_3,
                (unsigned long)uxTaskGetStackHighWaterMark(NULL));
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/* ═══════════════════════════════════════════════════════════════
 * 公共接口
 * ═══════════════════════════════════════════════════════════════ */
size_t inmp441_read_pcm(int16_t *out, size_t samples, uint32_t timeout_ms)
{
    if (!out || samples == 0) return 0;
    if (samples > INMP441_RING_SAMPLES) samples = INMP441_RING_SAMPLES;
    uint32_t start = xTaskGetTickCount() * portTICK_PERIOD_MS;
    while (s_pcm_filled < (int)samples) {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (xTaskGetTickCount() * portTICK_PERIOD_MS - start >= timeout_ms) break;
    }
    return ring_read_recent(out, (int)samples);
}

size_t audio_input_read_pcm(int16_t *out, size_t samples, uint32_t timeout_ms)
{
    return inmp441_read_pcm(out, samples, timeout_ms);
}

void inmp441_get_stats(float *rms_out, int16_t *peak_out, uint32_t *samples_out)
{
    if (rms_out)    *rms_out = s_cur_rms;
    if (peak_out)   *peak_out = s_cur_peak;
    if (samples_out) *samples_out = s_total_samples;
}

bool inmp441_mic_ok(void) { return s_mic_ok; }
