/**
 * TensorFlow Lite Micro 模型接口 (鼾声检测专用版)
 *
 * 单输入模型: 音频 MFCC 特征 (49 x 13)
 * 单输出模型: [snore_prob]
 */

#include "model_interface.h"
#include "app_config.h"
#include <string.h>
#include <math.h>

// TFLM 头文件
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/tflite_bridge/micro_error_reporter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include <inttypes.h>

static const char* TAG = "MODEL";

// 模型数据 (由 convert_model.py 生成)
#include "model_data.h"

// TFLM 对象 (显式全局变量，避免 static 局部变量的生命周期陷阱)
static tflite::MicroMutableOpResolver<20>* s_resolver = nullptr;
static tflite::MicroInterpreter* s_interpreter = nullptr;
static TfLiteTensor* s_input_tensor = nullptr;
static TfLiteTensor* s_output_tensor = nullptr;
static uint8_t* s_tensor_arena = nullptr;
static bool s_model_initialized = false;

// 模拟推理 fallback (当 TFLM 不可用时)
static inline float sigmoid(float x) {
    return 1.0f / (1.0f + expf(-x));
}

static void mock_inference(const float* audio_features, float* output) {
    float audio_energy = 0.0f;
    float energy_var = 0.0f;
    for (int i = 0; i < AUDIO_MFCCFrames_PER_INF; i++) {
        audio_energy += fabsf(audio_features[i * AUDIO_MFCC_NUM]);
    }
    audio_energy /= AUDIO_MFCCFrames_PER_INF;

    for (int i = 1; i < AUDIO_MFCCFrames_PER_INF; i++) {
        float diff = audio_features[i * AUDIO_MFCC_NUM] - audio_features[(i - 1) * AUDIO_MFCC_NUM];
        energy_var += diff * diff;
    }
    energy_var = sqrtf(energy_var / (AUDIO_MFCCFrames_PER_INF - 1));

    float low_freq_energy = 0.0f;
    float total_energy = 0.0f;
    for (int i = 0; i < AUDIO_MFCCFrames_PER_INF; i++) {
        for (int j = 0; j < 3 && j < AUDIO_MFCC_NUM; j++) {
            low_freq_energy += fabsf(audio_features[i * AUDIO_MFCC_NUM + j]);
        }
        for (int j = 0; j < AUDIO_MFCC_NUM; j++) {
            total_energy += fabsf(audio_features[i * AUDIO_MFCC_NUM + j]);
        }
    }
    float low_freq_ratio = total_energy > 0 ? low_freq_energy / total_energy : 0.5f;

    float score = 0.0f;
    score += (audio_energy - 2.0f) * 0.3f;
    score += energy_var * 0.2f;
    score += (low_freq_ratio - 0.3f) * 0.5f;

    output[0] = sigmoid(score);
}

bool model_interface_init(void) {
    if (s_model_initialized) {
        return true;
    }

    // 在 PSRAM 中分配张量空间
    if (!s_tensor_arena) {
        s_tensor_arena = (uint8_t*)heap_caps_malloc(TFLM_TENSOR_ARENA_SIZE, MALLOC_CAP_SPIRAM);
        if (!s_tensor_arena) {
            ESP_LOGE(TAG, "Failed to allocate tensor arena in PSRAM");
            return false;
        }
    }

    // 创建 resolver (只创建一次)
    if (!s_resolver) {
        s_resolver = new (std::nothrow) tflite::MicroMutableOpResolver<20>();
        if (!s_resolver) {
            ESP_LOGE(TAG, "Failed to allocate resolver");
            return false;
        }
    }

    // 注册模型所需算子
    TfLiteStatus reg_status;

    reg_status = s_resolver->AddConv2D();
    if (reg_status != kTfLiteOk) { ESP_LOGE(TAG, "AddConv2D failed"); return false; }

    reg_status = s_resolver->AddMaxPool2D();
    if (reg_status != kTfLiteOk) { ESP_LOGE(TAG, "AddMaxPool2D failed"); return false; }

    reg_status = s_resolver->AddMean();
    if (reg_status != kTfLiteOk) { ESP_LOGE(TAG, "AddMean failed"); return false; }

    reg_status = s_resolver->AddFullyConnected();
    if (reg_status != kTfLiteOk) { ESP_LOGE(TAG, "AddFullyConnected failed"); return false; }

    reg_status = s_resolver->AddLogistic();
    if (reg_status != kTfLiteOk) { ESP_LOGE(TAG, "AddLogistic failed"); return false; }

    reg_status = s_resolver->AddReshape();
    if (reg_status != kTfLiteOk) { ESP_LOGE(TAG, "AddReshape failed"); return false; }

    reg_status = s_resolver->AddRelu();
    if (reg_status != kTfLiteOk) { ESP_LOGE(TAG, "AddRelu failed"); return false; }

    reg_status = s_resolver->AddQuantize();
    if (reg_status != kTfLiteOk) { ESP_LOGE(TAG, "AddQuantize failed"); return false; }

    reg_status = s_resolver->AddDequantize();
    if (reg_status != kTfLiteOk) { ESP_LOGE(TAG, "AddDequantize failed"); return false; }

    reg_status = s_resolver->AddExpandDims();
    if (reg_status != kTfLiteOk) { ESP_LOGE(TAG, "AddExpandDims failed"); return false; }

    ESP_LOGI(TAG, "Registered %u ops", s_resolver->GetRegistrationLength());

    // 获取模型
    const tflite::Model* model = tflite::GetModel(g_model_data);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Model schema version mismatch: expected %" PRIu32 ", got %" PRIu32,
                 (uint32_t)TFLITE_SCHEMA_VERSION, model->version());
        return false;
    }

    // 创建 Interpreter (堆分配，避免 static 局部变量不重建的问题)
    if (!s_interpreter) {
        s_interpreter = new (std::nothrow) tflite::MicroInterpreter(
            model, *s_resolver, s_tensor_arena, TFLM_TENSOR_ARENA_SIZE);
        if (!s_interpreter) {
            ESP_LOGE(TAG, "Failed to allocate interpreter");
            return false;
        }
    }

    // 分配张量
    TfLiteStatus allocate_status = s_interpreter->AllocateTensors();
    if (allocate_status != kTfLiteOk) {
        ESP_LOGE(TAG, "AllocateTensors failed");
        delete s_interpreter;
        s_interpreter = nullptr;
        return false;
    }

    s_input_tensor = s_interpreter->input(0);
    s_output_tensor = s_interpreter->output(0);

    ESP_LOGI(TAG, "AllocateTensors success");
    ESP_LOGI(TAG, "Model input dims: %d", s_input_tensor->dims->size);
    ESP_LOGI(TAG, "Model output dims: %d", s_output_tensor->dims->size);

    s_model_initialized = true;
    return true;
}

bool model_interface_invoke(const float* input_features, float* output_scores) {
    if (!s_model_initialized || !s_interpreter) {
        ESP_LOGW(TAG, "Model not initialized, using mock inference");
        mock_inference(input_features, output_scores);
        return true;
    }

    // 拷贝输入特征
    memcpy(s_input_tensor->data.f, input_features, AUDIO_FEATURE_SIZE * sizeof(float));

    // 执行推理
    TfLiteStatus invoke_status = s_interpreter->Invoke();
    if (invoke_status != kTfLiteOk) {
        ESP_LOGE(TAG, "Invoke failed");
        return false;
    }

    // 获取输出
    output_scores[0] = s_output_tensor->data.f[0];
    return true;
}
