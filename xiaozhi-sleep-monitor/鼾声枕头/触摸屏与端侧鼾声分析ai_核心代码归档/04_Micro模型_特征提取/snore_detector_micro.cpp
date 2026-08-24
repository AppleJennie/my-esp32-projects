/**
 * snore_detector_micro.cc
 *
 * 加载 snore-recognition micro model，运行推理。
 * 输入 uint8 [1, 1830]，输出 uint8 [1, 2]。
 * 用最近 3 次 snore_score 平均值 > 128 判定。
 */

#include "snore_detector_micro.h"
#include "snore_micro_feature_adapter.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include <cstdlib>
#include <new>

static const char *TAG = "SNORE_MICRO";

/* ── Arena ── */
#ifndef MICRO_ARENA_SIZE
#define MICRO_ARENA_SIZE  (256 * 1024)
#endif

struct snore_detector_micro_t {
    uint8_t                  *arena;
    size_t                    arena_size;
    tflite::MicroInterpreter *interpreter;
    TfLiteTensor             *input_tensor;
    TfLiteTensor             *output_tensor;
    bool                      valid;
    char                      err_reason[64];

    /* 平滑 buffer：最近 3 次 snore_score */
    uint8_t scores_buf[3];
    int     buf_idx;
};

/* ── Init ── */
extern "C" {

snore_detector_micro_t *snore_detector_micro_init(
    const uint8_t *model_data, uint32_t model_size, uint32_t arena_size)
{
    if (!model_data || model_size == 0) {
        ESP_LOGE(TAG, "model_data_missing");
        return NULL;
    }
    if (arena_size == 0) arena_size = MICRO_ARENA_SIZE;

    snore_detector_micro_t *det = (snore_detector_micro_t *)calloc(1, sizeof(*det));
    if (!det) return NULL;
    snprintf(det->err_reason, sizeof(det->err_reason), "not_init");

    /* Arena in PSRAM */
    det->arena = (uint8_t *)heap_caps_malloc(arena_size,
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!det->arena) {
        ESP_LOGE(TAG, "arena_malloc_failed (%u B)", (unsigned)arena_size);
        snprintf(det->err_reason, sizeof(det->err_reason), "arena_malloc_failed");
        free(det);
        return NULL;
    }
    det->arena_size = arena_size;

    /* Map model */
    const tflite::Model *model = tflite::GetModel(model_data);
    if (!model || model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "model schema mismatch");
        snprintf(det->err_reason, sizeof(det->err_reason), "schema_mismatch");
        free(det->arena);
        free(det);
        return NULL;
    }

    /* Op resolver: 11 ops from main_functions.cc */
    static tflite::MicroMutableOpResolver<11> resolver;
    resolver.AddConv2D();
    resolver.AddFullyConnected();
    resolver.AddReshape();
    resolver.AddMaxPool2D();
    resolver.AddShape();
    resolver.AddStridedSlice();
    resolver.AddPack();
    resolver.AddResizeBilinear();
    resolver.AddQuantize();
    resolver.AddDequantize();
    resolver.AddLogistic();

    /* Interpreter via placement new on static buffer */
    static uint8_t interp_buf[sizeof(tflite::MicroInterpreter)] __attribute__((aligned(16)));
    static bool interp_done = false;
    if (!interp_done) {
        det->interpreter = new (interp_buf) tflite::MicroInterpreter(
            model, resolver, det->arena, det->arena_size);
        interp_done = true;
    }

    if (!det->interpreter) {
        ESP_LOGE(TAG, "interpreter_create_failed");
        snprintf(det->err_reason, sizeof(det->err_reason), "interpreter_create_failed");
        free(det->arena);
        free(det);
        return NULL;
    }

    /* AllocateTensors */
    TfLiteStatus st = det->interpreter->AllocateTensors();
    if (st != kTfLiteOk) {
        ESP_LOGE(TAG, "allocate_tensors_failed arena=%u B (%.0f KB)",
                 (unsigned)arena_size, (float)arena_size / 1024.0f);
        snprintf(det->err_reason, sizeof(det->err_reason),
                 "allocate_tensors_failed_%uB", (unsigned)arena_size);
        free(det->arena);
        free(det);
        return NULL;
    }

    det->input_tensor  = det->interpreter->input(0);
    det->output_tensor = det->interpreter->output(0);

    if (!det->input_tensor || !det->output_tensor) {
        ESP_LOGE(TAG, "tensor_null");
        snprintf(det->err_reason, sizeof(det->err_reason), "tensor_null");
        free(det->arena);
        free(det);
        return NULL;
    }

    /* Validate input */
    if (det->input_tensor->type != kTfLiteUInt8 ||
        det->input_tensor->bytes != MICRO_FEATURE_ELEMENT_COUNT) {
        ESP_LOGE(TAG, "input mismatch: type=%d bytes=%d expected uint8/%d",
                 det->input_tensor->type, det->input_tensor->bytes,
                 MICRO_FEATURE_ELEMENT_COUNT);
        snprintf(det->err_reason, sizeof(det->err_reason), "input_mismatch");
        free(det->arena);
        free(det);
        return NULL;
    }

    det->valid = true;
    snprintf(det->err_reason, sizeof(det->err_reason), "ok");
    ESP_LOGI(TAG, "init OK arena=%u B input=%d bytes output=%d bytes",
             (unsigned)arena_size,
             det->input_tensor->bytes, det->output_tensor->bytes);
    return det;
}

/* ── Detect ── */

int snore_detector_micro_detect(snore_detector_micro_t *det,
                                 const uint8_t *input_features,
                                 snore_micro_result_t *result)
{
    if (!det || !input_features || !result) return -1;
    if (!det->valid) return -2;

    memset(result, 0, sizeof(*result));

    /* Copy input */
    memcpy(det->input_tensor->data.uint8, input_features,
           MICRO_FEATURE_ELEMENT_COUNT);

    /* Invoke */
    uint32_t t0 = xTaskGetTickCount() * portTICK_PERIOD_MS;
    TfLiteStatus st = det->interpreter->Invoke();
    result->inference_time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS - t0;

    if (st != kTfLiteOk) {
        ESP_LOGE(TAG, "Invoke failed");
        return -3;
    }

    const uint8_t *out = det->output_tensor->data.uint8;
    result->snore_score      = out[0];
    result->background_score = out[1];
    result->snore_prob       = (float)out[0] / 255.0f;

    /* Smoothing: 最近 3 次平均值 > 128 */
    det->scores_buf[det->buf_idx] = out[0];
    det->buf_idx = (det->buf_idx + 1) % 3;
    uint16_t avg = ((uint16_t)det->scores_buf[0]
                 + (uint16_t)det->scores_buf[1]
                 + (uint16_t)det->scores_buf[2]) / 3;
    result->is_snoring = (avg > 128);

    return 0;
}

void snore_detector_micro_deinit(snore_detector_micro_t *det) {
    if (!det) return;
    free(det->arena);
    free(det);
}

const char *snore_detector_micro_err(snore_detector_micro_t *det) {
    if (!det) return "null";
    return det->err_reason;
}

} /* extern "C" */
