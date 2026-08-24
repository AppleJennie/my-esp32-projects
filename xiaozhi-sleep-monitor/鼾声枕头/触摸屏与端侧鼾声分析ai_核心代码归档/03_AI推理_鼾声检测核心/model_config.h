/**
 * model_config.h — 鼾声模型统一配置
 */
#ifndef MODEL_CONFIG_H
#define MODEL_CONFIG_H

/* ================================================================
 * 主模型选择（二选一）
 * ================================================================ */
#define APP_USE_MICRO_SNORE_MODEL      1   /* micro模型 + 声学分类器共存 */
#define APP_USE_LEGACY_SNORE_MODEL     0

/* ================================================================
 * 推理行为
 * ================================================================ */
#define APP_SNORE_MODEL_ONLY_TEST      1
#define APP_ENABLE_ACOUSTIC_FALLBACK   0

/* ================================================================
 * 模型参数（自动）
 * ================================================================ */
#if APP_USE_MICRO_SNORE_MODEL
  #define APP_MODEL_NAME              "micro"
  #define APP_MODEL_INPUT_BYTES       1830
  #define APP_MODEL_INPUT_TYPE        "uint8"
  #define APP_MODEL_INFER_PERIOD_MS   4000
#elif APP_USE_LEGACY_SNORE_MODEL
  #define APP_MODEL_NAME              "legacy_4160"
  #define APP_MODEL_INPUT_BYTES       4160
  #define APP_MODEL_INPUT_TYPE        "float"
  #define APP_MODEL_INFER_PERIOD_MS   2000
#endif

/* ================================================================
 * 通用 PCM / FFT / 信号处理常量
 * ================================================================ */
#define SNORE_SAMPLES_PER_WINDOW       16000
#define SNORE_INPUT_SIZE               4160
#define SNORE_FFT_BINS                 65      /* 256点实数FFT正频率bin数: 0..4000Hz, 64帧×65=4160 */
#define SNORE_OUTPUT_SIZE              2

#define SNORE_FRAME_LENGTH_SAMPLES     512
#define SNORE_FRAME_STRIDE_SAMPLES     246
#define SNORE_FRAMES_PER_WINDOW        64
#define SNORE_FFT_SIZE                 256
#define SNORE_NOISE_FLOOR_LINEAR       0.001f
#define SNORE_NOISE_FLOOR_DB          -60
#define SNORE_TARGET_SAMPLE_RATE       16000

/* ================================================================
 * Legacy TFLite 量化参数 (编译用, 模型不匹配时自动降级到分类器)
 * ================================================================ */
#define SNORE_INPUT_SCALE              0.05f
#define SNORE_INPUT_ZERO_POINT         0
#define SNORE_OUTPUT_SCALE             0.00390625f
#define SNORE_OUTPUT_ZERO_POINT        -128

#endif
