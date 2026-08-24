/**
 * 表情渲染：用程序绘制的大眼睛机器人表情（无需字库/图片资源）
 *
 * 每种表情 = 双眼 + 嘴型 + 可选眉毛/腮红，按行带（band）渲染后经 QSPI 刷屏。
 * 想换成图片/GIF 表情时，只需保留 emoji_scene_t 接口、替换本文件的绘制实现
 *（例如接入 LVGL + lv_gif，见 README）。
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EMOJI_NEUTRAL = 0,  /* 普通（会周期性眨眼） */
    EMOJI_HAPPY,        /* 开心 */
    EMOJI_ANGRY,        /* 生气 */
    EMOJI_SLEEPY,       /* 困倦 */
    EMOJI_LISTENING,    /* 聆听中（睁大眼睛） */
    EMOJI_EXPR_COUNT
} emoji_expr_t;

typedef struct {
    emoji_expr_t expr;
    float eye_open;     /* 眼睛睁开程度：0.0(闭) ~ 1.0(正常)，>1 为睁大 */
    int battery_pct;    /* 电量 0~100；传负数则不显示电量图标 */
} emoji_scene_t;

/** 分配行带缓冲区，只需调用一次 */
esp_err_t emoji_init(void);

/** 按场景重绘整屏（阻塞直到刷屏完成） */
void emoji_draw(const emoji_scene_t *scene);

#ifdef __cplusplus
}
#endif
