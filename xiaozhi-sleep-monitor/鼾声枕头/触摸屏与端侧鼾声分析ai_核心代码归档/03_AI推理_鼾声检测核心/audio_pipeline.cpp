/**
 * audio_pipeline.cc — PCM → 特征提取 → TFLite 推理 → 声学分类 → audio_feature_t
 *
 * 每秒运行一次。所有大缓冲在 init 时从 PSRAM 分配。
 * model=OFF 时：prob 显示 --，infer 显示 --，不伪造数据。
 */

/* Compile-time toggle — 模型选择统一在 model_config.h */
#include "model_config.h"
#define CONFIG_ENABLE_SNORE_CLASSIFIER 1
#define SNORE_TEST_MODE 1

#include "audio_pipeline.h"
#include "app_config.h"
#if CONFIG_ENABLE_ES8388_AUDIO
#include "audio_i2s.h"
#endif
#if CONFIG_ENABLE_INMP441_AUDIO
#include "inmp441_i2s.h"
#endif
#include "audio_processor.h"
#if APP_USE_MICRO_SNORE_MODEL
#include "snore_micro/snore_detector_micro.h"
#include "snore_micro/snore_micro_feature_adapter.h"
#include "snore_micro/snore_model_micro_data.h"
#elif APP_USE_LEGACY_SNORE_MODEL
#include "legacy_unused/snore_detector.h"
#include "legacy_unused/snore_model_data.h"
#endif
#include "snore_classifier.h"
#include "snore_audio_analyzer.h"
#include "sleep_data.h"
#include "fusion_types.h"
#include "esp_log.h"
#include "string.h"
#include "math.h"
#include "stdio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_heap_caps.h"

static const char *TAG      = "AUDIO_PIPE";
static const char *SNORE_TAG = "SNORE_RES";

/* ── Heap corruption debug: 禁用，防递归崩溃 ── */
#define CHECK_HEAP_STAGE(name, loop) do {} while (0)

/* ── 模块状态 ── */
static bool s_mic_valid     = false;
static int  s_mic_bad_sec   = 0;

#if CONFIG_ENABLE_TFLITE && !APP_USE_MICRO_SNORE_MODEL
static snore_detector_t *s_detector = NULL;
/* arena now allocated inside snore_detector_init */
static int  s_acoustic_on_count  = 0;
__attribute__((unused)) static int  s_acoustic_off_count = 0;
__attribute__((unused)) static bool s_snore_latched      = false;
__attribute__((unused)) static int  s_snore_off_count    = 0;
#endif
#if APP_USE_MICRO_SNORE_MODEL
static snore_detector_micro_t *s_micro_detector = NULL;
static uint8_t s_micro_feature_buf[MICRO_FEATURE_ELEMENT_COUNT];  /* 1830 bytes */
#endif
static bool              s_model_ok       = false;
static const char       *s_model_status   = "OFF";
static const char       *s_model_err_reason = "tflite_disabled";

/* ── PSRAM 缓冲（init 时分配，整个生命周期复用）── */
static int16_t     *s_pcm_work_buf    = NULL;  /* 16000+32 int16   */
static float       *s_pcm_float_buf   = NULL;  /* 16000+32 float   */
static float       *s_fft_features    = NULL;  /* 4160+32 float    */
static float       *s_fft_work_buf    = NULL;  /* 16000+32 float   */
static float       *s_fft_temp_buf    = NULL;  /* 256+32 float     */
static int16_t     *s_pcm_4s_buf      = NULL;  /* 64000 int16 = 4s, 由 audio_i2s_get_recent_pcm 填充 */
static int16_t     *s_selected_pcm    = NULL;  /* 16000 int16, 选中的窗口副本 */
/* 高级声学分析用的平均频谱。不要放栈上，避免任务栈压力。 */
static float        s_adv_fft_mag[SNORE_FFT_BINS];
/* SNORE_MODEL_TEST_MODE → model_config.h */
#define SNORE_DUMP_SELECTED_PCM 0  /* 1=保存选中 PCM 到 SD */

#define GUARD_MAGIC 0xDEADBEEF
#define GUARD_WORDS 8
static uint32_t *s_pcm_guard      = NULL;
static uint32_t *s_fft_feat_guard = NULL;
static uint32_t *s_fft_work_guard = NULL;
static uint32_t *s_fft_temp_guard = NULL;

/* ── Snore decision smoothing ── */
#define SNORE_ON_THRESHOLD        0.55f
#define SNORE_OFF_THRESHOLD       0.42f
#define SNORE_MIN_RMS_FOR_ON     35.0f
#define SNORE_MIN_PEAK_FOR_ON   120
#define SNORE_OFF_HOLD_FRAMES      2
#define SNORE_USE_ACOUSTIC_FALLBACK 0  /* 测试期间关闭 acoustic 兜底 */
#define SNORE_MODEL_PROB_FLOOR_FOR_ACOUSTIC 0.35f
/* APP_SNORE_MODEL_ONLY_TEST → model_config.h */
#define MICRO_INFER_PERIOD_MS 4000   /* micro 推理慢，4s 一次 */
#define AUDIO_FEATURE_STALE_MS 8000  /* micro 模式下 Mic stale 放宽到 8s */

static bool check_guard(const char *name, uint32_t *guard, unsigned long loop) {
    if (!guard) return true;
    bool ok = true;
    for (int i = 0; i < GUARD_WORDS; i++) {
        if (guard[i] != GUARD_MAGIC) {
            ESP_LOGE("GUARD",
                "%s guard[%d] corrupted: actual=0x%08lx expected=0x%08lx loop=%lu",
                name, i,
                (unsigned long)guard[i],
                (unsigned long)GUARD_MAGIC,
                loop);
            ok = false;
        }
    }
    return ok;
}
#define CHECK_GUARDS(lp) do { \
    check_guard("pcm",       s_pcm_guard,      (unsigned long)(lp)); \
    check_guard("fft_feat",  s_fft_feat_guard, (unsigned long)(lp)); \
    check_guard("fft_work",  s_fft_work_guard, (unsigned long)(lp)); \
    check_guard("fft_temp",  s_fft_temp_guard, (unsigned long)(lp)); \
} while (0)

/* ── 队列 ── */
static QueueHandle_t s_audio_queue = NULL;  /* 长度 1，xQueueOverwrite + xQueuePeek */

/* ── PSRAM 分配工具 ── */
static void *psram_alloc_or_die(size_t sz, const char *name) {
    void *p = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) {
        ESP_LOGE(TAG, "%s PSRAM alloc fail (%u B), trying DRAM...", name, (unsigned)sz);
        p = heap_caps_malloc(sz, MALLOC_CAP_8BIT);
    }
    if (!p) ESP_LOGE(TAG, "%s alloc FAILED (%u B) — module disabled", name, (unsigned)sz);
    return p;
}

/* ── Active model init is deliberately deferred into audio_pipeline_task.
 * Do NOT initialize TFLite models in audio_pipeline_init(), because FreeRTOS
 * task stacks are allocated from internal DRAM. Initializing both the old
 * Flutter model and the new micro model before xTaskCreate() left only about
 * 25KB internal RAM and caused:
 *   audio_pipe create FAILED (stack=16384)
 */
static bool audio_pipeline_init_active_model(void)
{
    if (s_model_status && strcmp(s_model_status, "OK") == 0) return s_model_ok;

    s_model_ok = false;
    s_model_status = "INIT";
    s_model_err_reason = "initing";

#if APP_USE_MICRO_SNORE_MODEL
    ESP_LOGI(TAG, "[SNORE_MICRO] active model init start");
    if (!snore_micro_feature_init()) {
        s_model_status = "ERR";
        s_model_err_reason = "micro_feature_init_failed";
        ESP_LOGE(TAG, "[SNORE_MICRO] feature adapter init failed");
        return false;
    }

    ESP_LOGI(TAG, "[SNORE_MICRO] init start, model=%u B", (unsigned)snore_micro_model_len);
    s_micro_detector = snore_detector_micro_init(
        snore_micro_model, snore_micro_model_len, 256 * 1024);

    if (!s_micro_detector) {
        s_model_status = "ERR";
        s_model_err_reason = "micro_detector_init_failed";
        ESP_LOGE(TAG, "[SNORE_MICRO] init failed");
        return false;
    }

    s_model_ok = true;
    s_model_status = "OK";
    s_model_err_reason = "ok";
    ESP_LOGI(TAG, "[SNORE_MICRO] init OK, input_bytes=%d", MICRO_FEATURE_ELEMENT_COUNT);
    return true;

#elif CONFIG_ENABLE_TFLITE
    ESP_LOGI(TAG, "[SNORE_MODEL] active model init start");
    ESP_LOGI(TAG, "[SNORE_MODEL] model size=%u B", (unsigned)snore_detection_tflite_len);
    ESP_LOGI(TAG, "[SNORE_MODEL] arena=PSRAM size=512KB");

    if (snore_detection_tflite_len < 10000) {
        s_model_status = "ERR";
        s_model_err_reason = "model_data_missing";
        ESP_LOGE(TAG, "[SNORE_MODEL] model_data_missing or too small (%u B)",
                 (unsigned)snore_detection_tflite_len);
        return false;
    }

    s_detector = snore_detector_init(
        (const uint8_t *)snore_detection_tflite,
        (uint32_t)snore_detection_tflite_len, 512 * 1024);
    if (!s_detector) {
        s_model_status = "ERR";
        s_model_err_reason = "detector_init_failed";
        ESP_LOGE(TAG, "[SNORE_MODEL] init failed");
        return false;
    }

    int in_dim = 0, out_dim = 0;
    bool valid = false;
    snore_detector_get_info(s_detector, &in_dim, &out_dim, &valid);
    if (!valid) {
        s_model_status = "ERR";
        s_model_err_reason = "input_dim_mismatch";
        ESP_LOGW(TAG, "[SNORE_MODEL] input_dim_mismatch: model=%d config=%d",
                 in_dim, SNORE_INPUT_SIZE);
        return false;
    }

    s_model_ok = true;
    s_model_status = "OK";
    s_model_err_reason = "ok";
    ESP_LOGI(TAG, "[SNORE_MODEL] input dims=%d (config=%d)", in_dim, SNORE_INPUT_SIZE);
    ESP_LOGI(TAG, "[SNORE_MODEL] output dims=%d (config=%d)", out_dim, SNORE_OUTPUT_SIZE);
    ESP_LOGI(TAG, "[SNORE_MODEL] init OK");
    return true;
#else
    s_model_status = "OFF";
    s_model_err_reason = "tflite_disabled";
    ESP_LOGI(TAG, "[SNORE_MODEL] disabled at compile time");
    return false;
#endif
}

/* ═══════════════════════════════════════════════════════════════
 * 初始化
 * ═══════════════════════════════════════════════════════════════ */

bool audio_pipeline_init(void)
{
    ESP_LOGI(TAG, "[PIPE_STAGE] before detector init micro=%d tflite=%d",
             APP_USE_MICRO_SNORE_MODEL, CONFIG_ENABLE_TFLITE);
    /* ── PSRAM 分配（数据 + GUARD_WORDS 个 uint32 guard）── */
    {
        size_t gsz = GUARD_WORDS * sizeof(uint32_t);
        size_t pcm_sz  = SNORE_SAMPLES_PER_WINDOW * sizeof(int16_t);
        size_t flt_sz  = SNORE_SAMPLES_PER_WINDOW * sizeof(float);
        size_t feat_sz = SNORE_INPUT_SIZE * sizeof(float);
        size_t tmp_sz  = 1024 * sizeof(float);  /* 扩大到 1024 float */

        uint8_t *raw;
        raw = (uint8_t *)psram_alloc_or_die(pcm_sz + gsz, "pcm_work");
        if (raw) { s_pcm_work_buf = (int16_t *)raw; s_pcm_guard = (uint32_t *)(raw + pcm_sz); }

        raw = (uint8_t *)psram_alloc_or_die(flt_sz + gsz, "pcm_float");
        if (raw) s_pcm_float_buf = (float *)raw;

        raw = (uint8_t *)psram_alloc_or_die(feat_sz + gsz, "fft_features");
        if (raw) { s_fft_features = (float *)raw; s_fft_feat_guard = (uint32_t *)(raw + feat_sz); }

        raw = (uint8_t *)psram_alloc_or_die(flt_sz + gsz, "fft_work");
        if (raw) { s_fft_work_buf = (float *)raw; s_fft_work_guard = (uint32_t *)(raw + flt_sz); }

        raw = (uint8_t *)psram_alloc_or_die(tmp_sz + gsz, "fft_temp");
        if (raw) { s_fft_temp_buf = (float *)raw; s_fft_temp_guard = (uint32_t *)(raw + tmp_sz); }

        /* 4 秒 PCM ring buffer，用于最佳窗口选择 */
        raw = (uint8_t *)psram_alloc_or_die(64000 * sizeof(int16_t), "pcm_4s");
        if (raw) { s_pcm_4s_buf = (int16_t *)raw; memset(s_pcm_4s_buf, 0, 64000*sizeof(int16_t)); }

        /* selected PCM buffer — 窗口选择后的独立副本 */
        raw = (uint8_t *)psram_alloc_or_die(16000 * sizeof(int16_t), "sel_pcm");
        if (raw) s_selected_pcm = (int16_t *)raw;
    }

    /* 初始化 guard magic */
    for (int i = 0; i < GUARD_WORDS; i++) {
        if (s_pcm_guard)      s_pcm_guard[i]      = GUARD_MAGIC;
        if (s_fft_feat_guard) s_fft_feat_guard[i] = GUARD_MAGIC;
        if (s_fft_work_guard) s_fft_work_guard[i] = GUARD_MAGIC;
        if (s_fft_temp_guard) s_fft_temp_guard[i] = GUARD_MAGIC;
    }

    if (!s_pcm_work_buf || !s_fft_features) {
        ESP_LOGE(TAG, "Critical buffer allocation failed — audio pipeline disabled");
        return false;
    }

    /* ── 队列（长度 1，用 xQueueOverwrite）── */
    s_audio_queue = xQueueCreate(1, sizeof(audio_feature_t));
    if (!s_audio_queue) {
        ESP_LOGE(TAG, "Queue create fail");
        return false;
    }

    /* ── 模型不在这里初始化 ──
     * 这里仅分配 PSRAM buffer 和队列。模型初始化放到 audio_pipeline_task()
     * 开头执行，这样 audio_pipe 任务栈会先成功创建，避免内部 DRAM 不足。
     */
    s_model_ok = false;
    s_model_status = "INIT";
    s_model_err_reason = "deferred_to_audio_task";

    ESP_LOGI(TAG, "Pipeline init done: model init deferred, active_micro=%d", APP_USE_MICRO_SNORE_MODEL);
    return true;
}

/* ═══════════════════════════════════════════════════════════════
 * 主任务：每秒运行一次
 * ═══════════════════════════════════════════════════════════════ */

void audio_pipeline_task(void *pvParameters)
{
    uint32_t loop_count = 0;
    uint32_t pcm_ok_count = 0;
    uint32_t pcm_skip_count = 0;
    uint32_t infer_ok_count = 0;
    TickType_t xLastWakeTime;

    ESP_LOGI(TAG, "[AUDIO_PIPE] task started stack=%u model=%s",
             (unsigned)uxTaskGetStackHighWaterMark(NULL), s_model_status);
    ESP_LOGI(TAG, "[PIPE_STAGE] enter task");

    ESP_LOGI(TAG, "[PIPE_STAGE] before active model init micro=%d", APP_USE_MICRO_SNORE_MODEL);
    bool active_model_ok = audio_pipeline_init_active_model();
    ESP_LOGI(TAG, "[PIPE_STAGE] after active model init ok=%d status=%s reason=%s",
             active_model_ok ? 1 : 0, s_model_status, s_model_err_reason);

    vTaskDelay(pdMS_TO_TICKS(2000));  /* 等 I2S buffer 填满 */
    ESP_LOGI(TAG, "[PIPE_STAGE] wait pcm done");

    xLastWakeTime = xTaskGetTickCount();

    while (1) {
        loop_count++;

        /* ── 取 1 秒 PCM ── */
        int got = audio_input_read_pcm(s_pcm_work_buf, SNORE_SAMPLES_PER_WINDOW, pdMS_TO_TICKS(1000));
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        audio_feature_t feat;
        memset(&feat, 0, sizeof(feat));
        feat.timestamp_ms    = now;
        feat.model_enabled   = (s_model_status[0] != 'O' || s_model_status[1] != 'F');
        feat.model_valid     = s_model_ok;

        if (loop_count <= 3) {
            ESP_LOGI(TAG, "[PIPE_STAGE] got pcm len=%d rms=%.0f model=%s",
                     got, feat.rms_energy, s_model_status);
        }

        /* ── PCM 不足 → invalid ── */
        if (got < SNORE_SAMPLES_PER_WINDOW / 2) {
            pcm_skip_count++;
            feat.audio_valid   = false;
            feat.mic_connected = true;
            xQueueOverwrite(s_audio_queue, &feat);
#if SNORE_TEST_MODE
            if (pcm_skip_count <= 5 || pcm_skip_count % 30 == 0) {
                ESP_LOGW(TAG, "[AUDIO_PIPE] no pcm frame got=%d skip=%lu ok=%lu loop=%lu",
                         got, (unsigned long)pcm_skip_count,
                         (unsigned long)pcm_ok_count, (unsigned long)loop_count);
            }
#endif
            vTaskDelay(pdMS_TO_TICKS(900));
            continue;
        }
        pcm_ok_count++;

        /* ── 获取最近 4 秒连续 PCM（一次性，线性时间顺序）── */
        int got4s = 0;
        int rb_filled_ms = 0;
        if (s_pcm_4s_buf) {
            got4s = (int)inmp441_read_pcm(s_pcm_4s_buf, 64000, pdMS_TO_TICKS(500));
            rb_filled_ms = (got4s * 1000) / INMP441_SAMPLE_RATE;
        }
        CHECK_HEAP_STAGE("pcm_get", loop_count);
        CHECK_GUARDS(loop_count);

        /* ── 麦克风连接状态 ── */
        feat.mic_connected = true;

        /* ── 时域特征（始终计算，轻量）── */
        float    rms_val = 0;
        int16_t  peak_val = 0;
        float    zcr_val = 0;
        audio_processor_time_features(s_pcm_work_buf, (size_t)got,
                                       &rms_val, &peak_val, &zcr_val);
        feat.rms_energy  = rms_val;
        feat.peak        = peak_val;
        feat.zcr         = zcr_val;
        feat.noise_floor = rms_val * 0.15f;
        feat.audio_valid = true;
        CHECK_HEAP_STAGE("time_features", loop_count);

        /* ── 频谱特征提取 ── */
        bool feature_ok = false;
        const char *feature_err_reason = "n/a";
        float spec_centroid = 0.0f;
        float low_freq      = 0.0f;
        float harmonic      = 0.0f;
#if !APP_USE_MICRO_SNORE_MODEL
        feature_err_reason = "unknown";

        /* 检查缓冲区有效性 */
        if (!s_pcm_work_buf) {
            feature_err_reason = "null_pcm";
        } else if (!s_fft_features || !s_fft_work_buf || !s_fft_temp_buf) {
            feature_err_reason = "buffer_null";
        } else if (got < 8000) {
            feature_err_reason = "pcm_too_short";
        } else {
            int fret = audio_processor_extract_features_safe(
                s_pcm_work_buf, (size_t)got,
                s_fft_features, s_fft_work_buf, s_fft_temp_buf);
            feature_ok = (fret == 0);
            if (!feature_ok) feature_err_reason = "fft_not_initialized";
        }
        CHECK_HEAP_STAGE("fft_extract", loop_count);
        CHECK_GUARDS(loop_count);

        if (feature_ok) {
            /* 从 4160 维频谱计算声学指标 */
            float total_e = 1e-6f, low_e = 0, weighted_f = 0;
            for (int f = 0; f < SNORE_INPUT_SIZE; f++) {
                float mag = fabsf(s_fft_features[f]);
                total_e += mag;
                float freq_hz = (float)(f % SNORE_FFT_BINS) * 123.0f;
                weighted_f += mag * freq_hz;
                if (freq_hz < 500.0f) low_e += mag;
            }
            spec_centroid = weighted_f / total_e;
            low_freq      = low_e / total_e;

            /* 谐波比 */
            int peak_count = 0;
            for (int f = 1; f < SNORE_INPUT_SIZE - 1; f++) {
                if (s_fft_features[f] > s_fft_features[f-1] &&
                    s_fft_features[f] > s_fft_features[f+1] &&
                    s_fft_features[f] > 0.1f) peak_count++;
            }
            harmonic = (peak_count > 3 && peak_count < 30) ? 0.35f + peak_count * 0.02f : 0.15f;
            if (harmonic > 0.8f) harmonic = 0.8f;

            feat.spectral_centroid = spec_centroid;
            feat.low_freq_ratio    = low_freq;
            feat.harmonic_ratio    = harmonic;
            feat.feature_valid     = true;
            feat.noise_too_high    = (rms_val < 3.0f);
        } else {
            /* FFT 失败但时域特征可用：从时域估算声学指标。
             * rms > 2 时用启发式估算，至少不是全 0 */
            feat.feature_valid = false;
            if (rms_val > 2.0f && got >= 4000) {
                /* 启发式估算：基于 zcr 和 rms 粗略推断 */
                spec_centroid = 300.0f + zcr_val * 2000.0f;  /* zcr 高 → 频率高 */
                low_freq      = 0.3f + (1.0f - zcr_val) * 0.4f;  /* zcr 低 → 低频多 */
                harmonic      = 0.15f;
                if (spec_centroid > 4000.0f) spec_centroid = 4000.0f;
                if (low_freq > 0.9f) low_freq = 0.9f;
                feat.spectral_centroid = spec_centroid;
                feat.low_freq_ratio    = low_freq;
                feat.harmonic_ratio    = harmonic;
            }
        }

#else  /* APP_USE_MICRO_SNORE_MODEL: 从时域特征估算频谱指标 */
        /* 用 RMS/ZCR/Peak 估算声学特征, 供 snore_classifier 和 UI 显示使用。
         * micro 模型不走 4160 维 FFT 路径, 如果这里不写入 feat，
         * 后续 UI 会拿不到质心/低频/谐波等指标。
         */
        spec_centroid = 300.0f + zcr_val * 2000.0f;  /* ZCR高→频率高 */
        low_freq      = 0.5f - zcr_val * 0.4f;       /* ZCR低→低频多 */
        harmonic      = (peak_val > rms_val * 6.0f) ? 0.4f : 0.2f;
        if (spec_centroid > 4000.0f) spec_centroid = 4000.0f;
        if (low_freq < 0.1f)  low_freq = 0.1f;
        if (low_freq > 0.9f)  low_freq = 0.9f;
        feat.spectral_centroid = spec_centroid;
        feat.low_freq_ratio    = low_freq;
        feat.harmonic_ratio    = harmonic;
        feat.feature_valid     = true;
        feature_ok             = true;
        feature_err_reason     = "micro_estimated";
#endif

        /* ── type_hint ── */
        const char *type_hint = "none";
#if !APP_USE_MICRO_SNORE_MODEL
        if (feature_ok && rms_val > 5.0f) {
            if (spec_centroid >= 150.0f && spec_centroid <= 600.0f && low_freq > 0.5f)
                type_hint = "throat_like";
            else if (spec_centroid >= 600.0f && spec_centroid <= 1500.0f && harmonic > 0.35f)
                type_hint = "nasal_like";
            else if (spec_centroid > 1500.0f && harmonic < 0.2f)
                type_hint = "mouth_breath_like";
        }
        }
#endif /* !APP_USE_MICRO_SNORE_MODEL */
        feat.type_hint = type_hint;

        /* ── TFLite 推理 ── */
        feat.inference_time_ms = 0;
        feat.snore_prob        = 0.0f;
        feat.is_snoring        = false;
        feat.snore_type        = 0;
        feat.snore_type_confidence = 0.0f;

#if APP_USE_MICRO_SNORE_MODEL
        /* ── Micro model 推理 ── */
        {
            ESP_LOGD(TAG, "[PIPE_STAGE] before micro feature");
            snore_micro_result_t mr;
            memset(&mr, 0, sizeof(mr));
            __attribute__((unused)) const char *micro_reason = "ok";

                        /* ── 从 4 秒 buffer 选最佳 1 秒窗口 ──
             * got4s 是线性时间顺序：buf[0]=最旧, buf[got4s-1]=最新
             * cand0: start=got4s-16000 (最新0~1s), age=0ms
             * cand1: start=got4s-32000 (1~2s前), age=1000ms
             * cand2: start=got4s-48000 (2~3s前), age=2000ms
             * cand3: start=0           (3~4s前), age=3000ms */
            int16_t *best_pcm = s_pcm_work_buf;  /* fallback */
            int best_win = -1;
            float best_score = -999.0f;
            float sel_rms = 0, sel_peak = 0, sel_zcr = 0;
            float cand_rms[4] = {0}, cand_peak[4] = {0}, cand_score[4] = {0};
            float cand_zcr[4] = {0};
            uint32_t cand_cksum[4] = {0};
            int selected_age_ms = 0;
            int max_win = (got4s >= 64000) ? 4 : (got4s >= 16000 ? 1 : 0);

            for (int w = 0; w < max_win; w++) {
                int start = got4s - 16000 * (w + 1);
                if (start < 0) start = 0;
                int16_t *win = s_pcm_4s_buf + start;
                float sum_sq = 0; int16_t pk = 0; int zc = 0; uint32_t ck = 0;
                for (int i = 0; i < 16000; i++) {
                    int16_t v = win[i];
                    sum_sq += (float)v * v;
                    if (v > pk) pk = v; else if (-v > pk) pk = -v;
                    if (i > 0 && ((win[i-1] ^ v) < 0)) zc++;
                    if (i < 100) ck += (uint32_t)(uint16_t)v;
                }
                cand_rms[w]   = sqrtf(sum_sq / 16000.0f);
                cand_peak[w]  = (float)pk;
                cand_zcr[w]   = (float)zc / 16000.0f;
                cand_cksum[w] = ck;
                float rms_sc  = (cand_rms[w] / 300.0f);  if (rms_sc > 1.0f) rms_sc = 1.0f;
                float peak_sc = (cand_peak[w] / 5000.0f); if (peak_sc > 1.0f) peak_sc = 1.0f;
                float clip_pen = 0;
                if (pk > 28000)      clip_pen = 2.0f;
                else if (pk > 20000) clip_pen = 1.0f;
                else if (pk > 12000) clip_pen = 0.5f;
                float zcr_pen = 0;
                if (cand_zcr[w] > 0.35f)      zcr_pen = 1.5f;
                else if (cand_zcr[w] > 0.30f) zcr_pen = 0.8f;
                else if (cand_zcr[w] > 0.25f) zcr_pen = 0.4f;
                int age = w * 1000;
                float age_f = 1.0f;
                if      (age > 3000) age_f = 0.55f;
                else if (age > 2000) age_f = 0.75f;
                else if (age > 1000) age_f = 0.90f;
                float sc = age_f * (0.35f * rms_sc + 0.15f * peak_sc
                                    - 0.35f * zcr_pen - 0.70f * clip_pen);
                cand_score[w] = sc;
                if (sc > best_score) { best_score = sc; best_win = w; }
            }
            if (best_win >= 0) {
                int start = got4s - 16000 * (best_win + 1);
                if (start < 0) start = 0;
                memcpy(s_selected_pcm, s_pcm_4s_buf + start, 16000 * sizeof(int16_t));
                best_pcm = s_selected_pcm;
                sel_rms  = cand_rms[best_win];
                sel_peak = cand_peak[best_win];
                sel_zcr  = cand_zcr[best_win];
                selected_age_ms = best_win * 1000;
            }
            bool micro_ok = snore_micro_generate_features(
                best_pcm, 16000, s_micro_feature_buf);
            ESP_LOGD(TAG, "[PIPE_STAGE] after micro feature ret=%d", micro_ok);
            /* P0: micro 特征生成成功 → feature_ok=true，修复日志 ERR */
            if (micro_ok) feature_ok = true;

            if (micro_ok && s_micro_detector) {
                ESP_LOGD(TAG, "[PIPE_STAGE] before micro invoke");
                int dret = snore_detector_micro_detect(
                    s_micro_detector, s_micro_feature_buf, &mr);
                ESP_LOGD(TAG, "[PIPE_STAGE] after micro invoke ret=%d infer=%lums",
                         dret, (unsigned long)mr.inference_time_ms);

                if (dret == 0) {
                    feat.model_valid = true;
                    feat.snore_prob  = mr.snore_prob;
                    feat.inference_time_ms = mr.inference_time_ms;
                    feat.snore_type  = 0;
                    feat.snore_type_confidence = 0.0f;

                    /* ── P1/P2 稳定性状态机 ── */
                    #define MICRO_STRONG_THRESH 140  /* 原180, 放宽: 降低强鼾声门槛 */
                    #define MICRO_WEAK_THRESH    80  /* 原120, 放宽: 降低弱鼾声门槛 */
                    #define MICRO_HOLD_MS       10000  /* 原8000, 放宽: 延长保持时间 */
                    #define MICRO_OFF_COUNT_TH    2

                    enum { ST_IDLE, ST_SNORE_ACTIVE };
                    static int      s_micro_state     = ST_IDLE;
                    static uint32_t s_last_snore_ms   = 0;
                    static uint32_t s_episode_start_ms = 0;
                    static int      s_micro_off_count = 0;
                    /* P1: 最近 3 次 candidate_hit 历史 */
                    static bool     s_cand_hist[3] = {false, false, false};
                    static int      s_cand_idx = 0;

                    bool raw_hit  = (mr.snore_score >= MICRO_STRONG_THRESH);
                    bool weak_hit = (mr.snore_score >= MICRO_WEAK_THRESH
                                     && mr.snore_score < MICRO_STRONG_THRESH);
                    bool no_hit   = (mr.snore_score < MICRO_WEAK_THRESH);

                    /* P1: candidate_hit 门控：模型高 + zcr 低 (原zcr<0.15, 放宽至0.25) */
                    bool candidate_hit = raw_hit && (sel_zcr < 0.25f);

                    /* P1: 更新候选历史 */
                    s_cand_hist[s_cand_idx] = candidate_hit;
                    s_cand_idx = (s_cand_idx + 1) % 3;
                    int cand_count = 0;
                    for (int i = 0; i < 3; i++) if (s_cand_hist[i]) cand_count++;
                    bool enter_active = (cand_count >= 1);  /* 原>=2, 放宽: 1/3即可 */

                    /* 计数（仅用于退出判断） */
                    if (raw_hit || weak_hit) { s_micro_off_count = 0; }
                    else                     { s_micro_off_count++; }

                    /* ── 状态机 ── */
                    const char *src = "none";
                    const char *snore_label = "NO";
                    const char *suggest = "未检测到明显鼾声.";
                    bool final_snore = false;
                    bool hold_active = false;
                    uint32_t episode_ms = 0;

                    switch (s_micro_state) {
                    case ST_IDLE:
                        /* P1: 3次中≥2次 candidate_hit 才进入 ACTIVE */
                        if (enter_active) {
                            s_micro_state = ST_SNORE_ACTIVE;
                            s_last_snore_ms = now;
                            s_episode_start_ms = now;
                            s_micro_off_count = 0;
                            src = "model";
                            snore_label = "YES";
                            suggest = "检测到明显鼾声.";
                            final_snore = true;
                        } else {
                            if (raw_hit && !candidate_hit) {
                                ESP_LOGW(SNORE_TAG,
                                    "DIAG: candidate hit pending score=%u zcr=%.2f "
                                    "%d/3, not enter ACTIVE",
                                    mr.snore_score, sel_zcr, cand_count);
                            }
                            if (weak_hit) {
                                ESP_LOGW(SNORE_TAG,
                                    "DIAG: weak_hit ignored in IDLE score=%u",
                                    mr.snore_score);
                            }
                            src = "none";
                            snore_label = "NO";
                            suggest = "未检测到明显鼾声.";
                            final_snore = false;
                        }
                        break;

                    case ST_SNORE_ACTIVE:
                        episode_ms = now - s_episode_start_ms;
                        if (raw_hit) {
                            s_last_snore_ms = now;
                            s_micro_off_count = 0;
                            src = "model";
                            snore_label = "YES";
                            suggest = "检测到明显鼾声.";
                            final_snore = true;
                        } else if (weak_hit) {
                            if ((now - s_last_snore_ms) < MICRO_HOLD_MS) {
                                hold_active = true;
                                src = "hold";
                            } else {
                                src = "model_weak";
                            }
                            snore_label = "YES";
                            suggest = "检测到明显鼾声.";
                            final_snore = true;
                        } else if (no_hit) {
                            /* P2: 连续 2 次 snore_score<50 强制退出 (原80, 放宽) */
                            bool force_exit = (mr.snore_score < 50  /* 原80, 放宽 */
                                               && s_micro_off_count >= MICRO_OFF_COUNT_TH);
                            if ((now - s_last_snore_ms) < MICRO_HOLD_MS
                                && !force_exit) {
                                hold_active = true;
                                src = "hold";
                                snore_label = "YES";
                                suggest = "检测到明显鼾声.";
                                final_snore = true;
                            } else if (s_micro_off_count >= MICRO_OFF_COUNT_TH
                                       || force_exit) {
                                s_micro_state = ST_IDLE;
                                src = "none";
                                snore_label = "NO";
                                suggest = "未检测到明显鼾声.";
                                final_snore = false;
                            } else {
                                hold_active = true;
                                src = "hold";
                                snore_label = "YES";
                                suggest = "检测到明显鼾声.";
                                final_snore = true;
                            }
                        }
                        break;
                    }

                    feat.is_snoring = final_snore;
                    feat.model_snoring = final_snore;  /* 模型判决,不受分类器覆盖 */

                    /* ── 诊断: 自检一致性 ── */
                    if (mr.snore_score >= 180 && !final_snore) {
                        ESP_LOGE(SNORE_TAG,
                            "DIAG: score=%u but snore=NO! state=%d src=%s",
                            mr.snore_score, s_micro_state, src);
                    }
                    if (final_snore && strcmp(suggest, "检测到明显鼾声.") != 0) {
                        ESP_LOGE(SNORE_TAG,
                            "DIAG: snore=YES but suggest='%s'", suggest);
                    }

                    /* 选定窗口校验 */
                    uint32_t sel_cksum = 0;
                    if (s_selected_pcm && best_win >= 0) {
                        for (int i = 0; i < 100; i++)
                            sel_cksum += (uint32_t)(uint16_t)s_selected_pcm[i];
                    }

                    /* 检测 selected_pcm 是否长期不变 */
                    static uint32_t s_last_cksum = 0;
                    static int      s_same_cksum_count = 0;
                    static float    s_last_cur_rms = 0;
                    if (sel_cksum == s_last_cksum && sel_cksum != 0) {
                        s_same_cksum_count++;
                    } else {
                        s_same_cksum_count = 0;
                    }
                    if (s_same_cksum_count == 3 && got4s == 64000 &&
                        fabsf(feat.rms_energy - s_last_cur_rms) > 10.0f) {
                        ESP_LOGW(TAG, "DIAG: selected window may be reused "
                                 "(cksum=0x%04lX x%d, cur_rms changed %.0f->%.0f)",
                                 (unsigned long)(sel_cksum & 0xFFFF),
                                 s_same_cksum_count,
                                 s_last_cur_rms, feat.rms_energy);
                    }
                    if (s_same_cksum_count >= 4) {
                        ESP_LOGW(TAG, "DIAG: selected window reused too long "
                                 "(cksum=0x%04lX x%d)",
                                 (unsigned long)(sel_cksum & 0xFFFF),
                                 s_same_cksum_count);
                    }
                    s_last_cksum = sel_cksum;
                    s_last_cur_rms = feat.rms_energy;

                    ESP_LOGI(SNORE_TAG,
                        "mic=OK model=micro feature=%s "
                        "state=%s episode=%lums "
                        "sel_win=%d sel_age=%dms sel_cksum=0x%04lX "
                        "got4s=%d rb_fill=%dms "
                        "c0_ck=0x%04lX c1_ck=0x%04lX c2_ck=0x%04lX c3_ck=0x%04lX "
                        "sel_rms=%.0f sel_peak=%.0f sel_zcr=%.2f "
                        "cur_rms=%.0f cur_peak=%d "
                        "c0_sc=%.2f c1_sc=%.2f c2_sc=%.2f c3_sc=%.2f "
                        "snore_score=%u bg=%u sn_prob=%.2f "
                        "raw=%d weak=%d hold=%d on=%d off=%d "
                        "last_age=%lums "
                        "src=%s snore=%s infer=%lums suggest=%s",
                        feature_ok ? "OK" : "ERR",
                        s_micro_state == ST_SNORE_ACTIVE ? "ACTIVE" : "IDLE",
                        (unsigned long)episode_ms,
                        best_win, selected_age_ms,
                        (unsigned long)(sel_cksum & 0xFFFF),
                        got4s, rb_filled_ms,
                        (unsigned long)(cand_cksum[0] & 0xFFFF),
                        (unsigned long)(cand_cksum[1] & 0xFFFF),
                        (unsigned long)(cand_cksum[2] & 0xFFFF),
                        (unsigned long)(cand_cksum[3] & 0xFFFF),
                        sel_rms, sel_peak, sel_zcr,
                        feat.rms_energy, feat.peak,
                        cand_score[0], cand_score[1],
                        cand_score[2], cand_score[3],
                        mr.snore_score, mr.background_score,
                        mr.snore_prob,
                        raw_hit ? 1 : 0, weak_hit ? 1 : 0,
                        hold_active ? 1 : 0,
                        cand_count, s_micro_off_count,
                        (unsigned long)(now - s_last_snore_ms),
                        src, snore_label,
                        (unsigned long)mr.inference_time_ms, suggest);
#if SNORE_DUMP_SELECTED_PCM
                /* 保存选中 PCM 到 SD（最多 20 个文件） */
                if (s_selected_pcm && best_win >= 0) {
                    static int dump_idx = 0;
                    if (dump_idx < 20) {
                        char path[64];
                        snprintf(path, sizeof(path),
                                 "/sdcard/SLEEP/PCM/sel_%03d.raw", dump_idx);
                        FILE *fp = fopen(path, "wb");
                        if (fp) {
                            fwrite(s_selected_pcm, 1, 32000, fp);
                            fclose(fp);
                            ESP_LOGI(TAG, "PCM dump %s rms=%.0f peak=%.0f score=%u",
                                     path, sel_rms, sel_peak, mr.snore_score);
                        }
                        dump_idx++;
                    }
                }
#endif
                } else {
                    micro_reason = "invoke_failed";
                }
            } else if (!s_micro_detector) {
                micro_reason = "detector_null";
            } else {
                micro_reason = "feature_gen_failed";
            }

            if (mr.inference_time_ms > 3000) {
                ESP_LOGW(TAG, "micro infer too slow: %lums",
                         (unsigned long)mr.inference_time_ms);
            }
            ESP_LOGD(TAG, "[PIPE_STAGE] feature published");
            taskYIELD();
        }
        /* Fall through to xQueueOverwrite at end of loop */
#endif  /* APP_USE_MICRO_SNORE_MODEL */

#if CONFIG_ENABLE_TFLITE && !APP_USE_MICRO_SNORE_MODEL
        /* 在 invocation 外层声明，供 SNORE_RES 使用 */
        const char *src = "none";
        bool impulse_noise = false;
        bool media_like = false;
        bool continuous_media_noise = false;
        float noise_prob = 0.0f;

        if (s_model_ok && s_detector && feature_ok && s_fft_features) {
            snore_result_t sr;
            uint32_t t0_inf = xTaskGetTickCount() * portTICK_PERIOD_MS;
            int dret = snore_detector_detect(s_detector, s_pcm_work_buf,
                                              (size_t)got, s_fft_features,
                                              &sr, SNORE_ON_THRESHOLD);
            feat.inference_time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS - t0_inf;
            taskYIELD();  /* 长推理后主动让出 CPU */
            if (dret == 0) {
                feat.model_valid = true;
                feat.snore_prob  = sr.snoring_confidence;
                noise_prob       = sr.noise_confidence;

                /* ── 媒体/语音特征抑制 ── */
                media_like =
                    (zcr_val >= 0.25f && harmonic < 0.75f) ||
                    (spec_centroid > 3000.0f && zcr_val >= 0.22f);

                /* ── 咳嗽/冲击声 ── */
                impulse_noise =
                    (peak_val > 1500 && rms_val < 100.0f) ||
                    (zcr_val > 0.32f && harmonic < 0.55f);

                /* ── 声学候选（media/impulse 时抑制）── */
                bool acoustic_candidate = false;
#if SNORE_USE_ACOUSTIC_FALLBACK
                if (!media_like && !impulse_noise) {
                    acoustic_candidate =
                        (rms_val >= 80.0f && peak_val >= 700 && harmonic >= 0.35f) ||
                        (rms_val >= 120.0f && peak_val >= 1000 && low_freq >= 0.05f) ||
                        (rms_val >= 180.0f && peak_val >= 1500);
                }
#endif

                /* ── 连续声学计数 ── */
                if (acoustic_candidate) { s_acoustic_on_count++;  s_acoustic_off_count = 0; }
                else                     { s_acoustic_off_count++; s_acoustic_on_count  = 0; }

                /* ── 模型判决 ── */
                bool model_on  = (feat.snore_prob >= SNORE_ON_THRESHOLD)
                              && (rms_val >= SNORE_MIN_RMS_FOR_ON)
                              && (peak_val >= SNORE_MIN_PEAK_FOR_ON);
                bool model_off = (feat.snore_prob <= SNORE_OFF_THRESHOLD)
                              || (rms_val < 18.0f) || (peak_val < 60);

                /* ── 连续媒体噪声：acoustic 持续 ≥5 帧 → 非鼾声 ── */
                continuous_media_noise = (s_acoustic_on_count >= 5);

                /* ── 声学允许条件 ── */
                bool acoustic_allowed =
#if SNORE_USE_ACOUSTIC_FALLBACK
                    (feat.snore_prob >= SNORE_MODEL_PROB_FLOOR_FOR_ACOUSTIC) &&
                    !media_like &&
                    !continuous_media_noise;
#else
                    false;
#endif

                bool acoustic_snore = acoustic_allowed && acoustic_candidate
                                   && (s_acoustic_on_count >= 2);

                /* ── src 判定 ── */
                if (model_on)
                    src = "model";
                else if (acoustic_snore)
                    src = "acoustic";
                else if (media_like || continuous_media_noise) {
                    src = "media";
                    s_acoustic_on_count = 0;  /* 重置，避免累积 */
                } else if (feat.is_snoring)
                    src = "hold";
                else
                    src = "none";

                bool instant_on  = model_on || acoustic_snore;
                bool instant_off = model_off && !acoustic_snore;

                if (instant_on) {
                    s_snore_latched = true;
                    s_snore_off_count = 0;
                } else if (instant_off) {
                    if (s_snore_off_count < SNORE_OFF_HOLD_FRAMES) {
                        s_snore_off_count++;
                    }
                    if (s_snore_off_count >= SNORE_OFF_HOLD_FRAMES) {
                        s_snore_latched = false;
                    }
                } else {
                    s_snore_off_count = 0;
                }

                feat.is_snoring = s_snore_latched;

#if APP_SNORE_MODEL_ONLY_TEST
                /* ── 裸测模式：仅 prob≥0.55 → snore=YES，其余全部 NO ── */
                {
                    bool model_snore = (feat.snore_prob >= SNORE_ON_THRESHOLD);
                    if (model_snore) {
                        src = "model";
                        feat.is_snoring = true;
                        s_snore_latched = true;
                        s_snore_off_count = 0;
                    } else {
                        src = "none";
                        feat.is_snoring = false;
                        s_snore_latched = false;
                        feat.snore_type = 0;
                        feat.snore_type_confidence = 0.0f;
                    }
                }
#endif
                /* 推理成功：model=OK 保持 */
            } else {
                /* 单次推理失败不永久禁用模型 */
                feat.model_valid = false;
#if SNORE_TEST_MODE
                static uint32_t inf_fail_count = 0;
                inf_fail_count++;
                if (inf_fail_count <= 3) {
                    ESP_LOGW(TAG, "Inference failed (dret=%d) count=%lu — model stays OK",
                             dret, (unsigned long)inf_fail_count);
                }
#endif
            }
            /* Debug: 每 30 秒打印一次模型输入前 10 个特征值 */
#if SNORE_TEST_MODE
            {
                static uint32_t last_dbg = 0;
                if (now - last_dbg >= 30000) {
                    last_dbg = now;
                    char dbg[128]; int pos = 0;
                    pos += snprintf(dbg + pos, sizeof(dbg) - pos, "feat[0..9]=");
                    for (int i = 0; i < 10 && pos < 120; i++)
                        pos += snprintf(dbg + pos, sizeof(dbg) - pos, "%.2f ", s_fft_features[i]);
                    ESP_LOGI(TAG, "%s", dbg);
                }
            }
#endif
        } else {
            /* model 未运行 */
#if SNORE_TEST_MODE
            static uint32_t last_no_model_log = 0;
            if (now - last_no_model_log >= 10000) {
                last_no_model_log = now;
                ESP_LOGW(TAG, "[AUDIO_PIPE] model not invoked reason=%s model_ok=%d det=%p feat_ok=%d fft=%p",
                         s_model_err_reason, s_model_ok, (void*)s_detector,
                         feature_ok, (void*)s_fft_features);
            }
#endif
        }
#endif
        CHECK_HEAP_STAGE("model_invoke", loop_count);
        CHECK_GUARDS(loop_count);

        /* ── 鼾声分类器（snore_classifier — 始终工作, 不依赖TFLite模型）── */
#if CONFIG_ENABLE_SNORE_CLASSIFIER
        {
            bool effective_snoring = feat.is_snoring;
            /* model OFF 或 ERR 时: 用声学特征自行判断 */
            if (!feat.model_enabled || !feat.model_valid) {
                effective_snoring = (strcmp(type_hint, "none") != 0) && feature_ok && rms_val > 10.0f;
            }
            /* ★ 模型说NO时, 需同时满足高能+低ZCR+模型有基础分才触发 ★ */
            if (!effective_snoring && feat.audio_valid
                && rms_val > 500.0f && zcr_val < 0.15f
                && feat.snore_prob > 0.2f) {
                effective_snoring = true;
            }

            if (effective_snoring && feat.audio_valid) {
                snore_classification_t sc;
                snore_classifier_classify(spec_centroid, low_freq, harmonic,
                                           true, &sc);
                feat.snore_type            = (int)sc.type;
                feat.snore_type_confidence  = sc.confidence;
                if (!feat.is_snoring) {
                    /* 分类器覆盖模型需rms>400 (排除普通语音/环境声) */
                    if (sc.type != SNORE_TYPE_NONE && rms_val > 400.0f) {
                        feat.is_snoring = true;
                        feat.snore_prob = sc.confidence;
                    }
                }
            }
        }
#endif

        /* ── 气流/恢复呼吸检测 ── */
        feat.airflow_sound_present   = (feat.zcr > 0.05f && feat.rms_energy > 50.0f);
        feat.recovery_breath_sound   = (feat.rms_energy > feat.noise_floor * 5.0f && feat.zcr < 0.03f);

        /* ── 高级声学分析：只产出 audio_feature_t，不直接改 UI 全局统计 ──
         * 之前这里直接写 g_sleep_data 并且每轮自增类型次数，导致：
         *   1) audio_pipeline 与 data_adapter 两处同时维护统计，容易不同步；
         *   2) micro 模式下 adv 没有 FFT 时类型常为 NONE/UNKNOWN，类型次数不涨；
         *   3) data_adapter 后面又会用 afeat.snore_type 覆盖 UI 当前类型。
         * 现在统一改为：audio_pipeline 只填充 feat，所有“次数/时长/上报”由
         * sleep_monitor_data_adapter.c 统一维护。
         */
        {
            snore_advanced_audio_t adv;
            memset(&adv, 0, sizeof(adv));

            const int fft_bins_per_frame = SNORE_FFT_BINS;  /* 65 */
            const float bin_hz = (float)SNORE_TARGET_SAMPLE_RATE / (float)SNORE_FFT_SIZE;

#if !APP_USE_MICRO_SNORE_MODEL
            if (feature_ok && s_fft_features) {
                /* 对 64 帧频谱求平均，避免只拿第 1 帧导致类型跳动。 */
                for (int b = 0; b < SNORE_FFT_BINS; b++) {
                    float acc = 0.0f;
                    for (int fr_i = 0; fr_i < SNORE_FRAMES_PER_WINDOW; fr_i++) {
                        acc += s_fft_features[fr_i * SNORE_FFT_BINS + b];
                    }
                    s_adv_fft_mag[b] = acc / (float)SNORE_FRAMES_PER_WINDOW;
                }
                SnoreAudioAnalyzer_Update(
                    s_adv_fft_mag, fft_bins_per_frame, bin_hz,
                    (uint16_t)rms_val, (uint16_t)peak_val, (uint8_t)(zcr_val * 100.0f),
                    feat.is_snoring ? 1 : 0,
                    (uint8_t)(feat.snore_prob * 255.0f),
                    feat.audio_valid ? 1 : 0, 0, now, &adv);
            } else
#endif
            {
                /* micro 模式没有 4160 维 FFT，保留上面 RMS/ZCR 估计出的声学指标。 */
                SnoreAudioAnalyzer_Update(
                    NULL, 0, bin_hz,
                    (uint16_t)rms_val, (uint16_t)peak_val, (uint8_t)(zcr_val * 100.0f),
                    feat.is_snoring ? 1 : 0,
                    (uint8_t)(feat.snore_prob * 255.0f),
                    feat.audio_valid ? 1 : 0, 0, now, &adv);
            }

            /* 有真实频谱结果时，用 adv 覆盖估计值；micro/无FFT时保留估计值。 */
            if (adv.spectral_centroid_hz > 0) {
                feat.spectral_centroid = (float)adv.spectral_centroid_hz;
            }
            if (adv.low_freq_ratio_x100 > 0) {
                feat.low_freq_ratio = (float)adv.low_freq_ratio_x100 / 100.0f;
            }
            if (adv.harmonic_ratio_x100 > 0) {
                feat.harmonic_ratio = (float)adv.harmonic_ratio_x100 / 100.0f;
            }

            feat.airflow_sound_present  = adv.airflow_sound_present ? true : feat.airflow_sound_present;
            feat.recovery_breath_sound  = adv.recovery_breath_sound ? true : feat.recovery_breath_sound;

            /* 类型优先级：有效 adv 类型 > 规则分类器已有类型；无鼾声时强制 none。 */
            if (feat.is_snoring && adv.snore_type >= 1 && adv.snore_type <= 4) {
                float adv_conf = (float)adv.type_confidence / 100.0f;
                if (!(feat.snore_type >= 1 && feat.snore_type <= 4)
                    || adv_conf > feat.snore_type_confidence) {
                    feat.snore_type = adv.snore_type;
                    feat.snore_type_confidence = adv_conf;
                }
            }
            if (!feat.is_snoring) {
                feat.snore_type = 0;
                feat.snore_type_confidence = 0.0f;
            }
        }

        /* ── 特征刚生成，年龄清零；消费端按 now-ts 计算 ── */
        feat.model_age_ms = 0;

        /* ── 麦克风有效性判断 ── */
        bool mic_bad = (feat.peak <= 2) || (feat.rms_energy <= 2.0f);
        if (mic_bad) s_mic_bad_sec++;
        else         s_mic_bad_sec = 0;
        s_mic_valid = (s_mic_bad_sec < 3);

        if (!s_mic_valid) {
            feat.audio_valid = false;
            feat.is_snoring  = false;
            feat.snore_prob  = 0.0f;
            feat.snore_type  = 0;
        }

        /* ═══════════════════════════════════════════════════════════════
         * SNORE_RES 日志输出（每秒一次）
         * ═══════════════════════════════════════════════════════════════ */

        if (s_mic_valid) {
            __attribute__((unused)) const char *feat_label  = feature_ok ? "OK" : "ERR";

#if CONFIG_ENABLE_TFLITE && !APP_USE_MICRO_SNORE_MODEL
            if (feat.model_enabled && feat.model_valid) {
                const char *snore_label = feat.is_snoring ? "YES" : "NO";
                const char *type_str    = feat.is_snoring ? snore_type_short_name(feat.snore_type) : "none";
                const char *suggest     = feat.is_snoring ? snore_type_suggest(feat.snore_type)
                                                          : "未检测到明显鼾声.";
                bool model_floor_ok = (feat.snore_prob >= SNORE_MODEL_PROB_FLOOR_FOR_ACOUSTIC);
                bool media_flag = media_like || continuous_media_noise;
                ESP_LOGI(SNORE_TAG,
                    "mic=OK model=OK feature=%s rms=%.0f peak=%d "
                    "centroid=%.0fHz low=%.2f harmonic=%.2f zcr=%.2f "
                    "noise_prob=%.2f snore_prob=%.2f src=%s model_only=1 "
                    "acoustic_on=%d media=%d model_floor=%d "
                    "snore=%s type=%s conf=%.2f infer=%lums suggest=%s",
                    feat_label, feat.rms_energy, feat.peak,
                    spec_centroid, low_freq, harmonic, feat.zcr,
                    (double)noise_prob, (double)feat.snore_prob, src,
                    s_acoustic_on_count,
                    media_flag ? 1 : 0, model_floor_ok ? 1 : 0,
                    snore_label, type_str,
                    feat.snore_type_confidence,
                    (unsigned long)feat.inference_time_ms, suggest);
            } else
#endif
            {
                /* model=OFF 或 ERR 时不显示 prob 和 infer。
                 * micro 模式下自有 SNORE_RES，跳过此旧路径 */
                if (APP_USE_MICRO_SNORE_MODEL) {
                    /* skip — micro model already printed above */
                } else if (feature_ok) {
                    const char *hint_explain = "none";
                    if (strcmp(type_hint, "throat_like") == 0)
                        hint_explain = "type_hint=throat_like(150-600Hz,low>0.5)";
                    else if (strcmp(type_hint, "nasal_like") == 0)
                        hint_explain = "type_hint=nasal_like(600-1500Hz,harmonic>0.35)";
                    else if (strcmp(type_hint, "mouth_breath_like") == 0)
                        hint_explain = "type_hint=mouth_breath_like(>1500Hz,harmonic<0.2)";

                    ESP_LOGI(SNORE_TAG,
                        "mic=OK model=%s feature=OK rms=%.0f peak=%d "
                        "centroid=%.0fHz low=%.2f harmonic=%.2f zcr=%.2f "
                        "prob=-- src=-- snore=-- %s suggest=模型未启用,仅显示声学特征",
                        s_model_status,
                        feat.rms_energy, feat.peak,
                        spec_centroid, low_freq, harmonic, feat.zcr,
                        hint_explain);
                } else {
                    ESP_LOGW(SNORE_TAG,
                        "mic=OK model=%s feature=ERR reason=%s rms=%.0f peak=%d "
                        "suggest=音频特征提取失败,请检查FFT/缓冲区",
                        s_model_status, feature_err_reason,
                        feat.rms_energy, feat.peak);
                }
            }
        } else {
            ESP_LOGW(SNORE_TAG,
                "mic=INVALID rms=%.1f peak=%d prob=-- snore=-- type=none "
                "reason=signal_not_changing "
                "suggest=检查INMP441供电/LR声道(LEFT=R GND RIGHT=3.3V)/BCLK12/WS11/SD13/I2S位宽32bit/slot切换",
                feat.rms_energy, feat.peak);
        }

        /* ── 推入队列 ── */
        /* 跟踪推理成功次数 */
        if (feat.model_valid && feat.inference_time_ms > 0) infer_ok_count++;

        /* ── 5秒诊断日志 [AUDIO] ── */
        {
            static uint32_t last_audio_diag = 0;
            if (now - last_audio_diag >= 5000) {
                last_audio_diag = now;
                ESP_LOGI(TAG,
                    "[AUDIO] valid=%d rms=%.0f peak=%d zcr=%.2f prob=%.2f "
                    "snore=%d type=%d age=%lu",
                    feat.audio_valid ? 1 : 0,
                    feat.rms_energy, feat.peak, feat.zcr,
                    feat.audio_valid ? feat.snore_prob : 0.0f,
                    feat.is_snoring ? 1 : 0,
                    feat.snore_type,
                    (unsigned long)feat.inference_time_ms);
            }
        }

        xQueueOverwrite(s_audio_queue, &feat);
        CHECK_GUARDS(loop_count);

        /* ── 每 3 次循环 alive log，启动时立即打印 ── */
        {
            if (loop_count <= 3 || (loop_count % 3) == 0) {
                ESP_LOGI(TAG, "[AUDIO_PIPE] alive loop=%lu pcm_recv=%lu infer_ok=%lu "
                         "stack_hwm=%lu model=%s micro=%d",
                         (unsigned long)loop_count,
                         (unsigned long)pcm_ok_count,
                         (unsigned long)infer_ok_count,
                         (unsigned long)uxTaskGetStackHighWaterMark(NULL),
                         s_model_status, APP_USE_MICRO_SNORE_MODEL);
            }
        }

        /* ── 固定 2 秒周期，让出 CPU 给 idle task ── */
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(
#if APP_USE_MICRO_SNORE_MODEL
            MICRO_INFER_PERIOD_MS
#else
            2000
#endif
        ));
    }
}

/* ═══════════════════════════════════════════════════════════════
 * 外部读取
 * ═══════════════════════════════════════════════════════════════ */

bool audio_pipeline_get_feature(audio_feature_t *out)
{
    if (!out || !s_audio_queue) return false;
    if (xQueuePeek(s_audio_queue, out, 0) == pdTRUE) return true;
    return false;
}

bool audio_pipeline_model_ok(void)       { return s_model_ok; }
const char *audio_pipeline_model_status(void) { return s_model_status; }
bool audio_pipeline_mic_ok(void)         { return s_mic_valid; }
