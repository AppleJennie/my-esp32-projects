#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <math.h>

static inline float sigmoid(float x) {
    return 1.0f / (1.0f + expf(-x));
}

// 快速近似 log2
static inline float fast_log2(float x) {
    union { float f; int i; } vx = { x };
    union { int i; float f; } mx;
    mx.i = (vx.i & 0x007FFFFF) | 0x3f000000;
    float y = vx.i * 1.1920928955078125e-7f;
    return y - 124.22551499f - 1.498030302f * mx.f
           - 1.72587999f / (0.3520887068f + mx.f);
}

static inline float fast_logf(float x) {
    return fast_log2(x) * 0.69314718f;
}

#ifdef __cplusplus
}
#endif
