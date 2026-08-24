#ifndef SLEEP_ALGORITHM_H
#define SLEEP_ALGORITHM_H

#include <stdint.h>
#include <stdbool.h>
#include "sleep_radar_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 体动等级 */
typedef enum {
    MOTION_LOW    = 0,
    MOTION_MEDIUM = 1,
    MOTION_HIGH   = 2,
} motion_level_t;

/* 算法输出 */
typedef struct {
    bool            inferred_presence;
    breath_status_t inferred_breath_status;
    motion_level_t  body_motion_level;
    uint8_t         simple_risk_score;  /* 0~100，暂简单实现 */
    presence_source_t presence_source;
} sleep_calc_t;

/* 从雷达快照计算推断值 */
void sleep_algorithm_calc(const sleep_radar_data_t *radar, sleep_calc_t *out);

/* 初始化 */
void sleep_algorithm_init(void);

#ifdef __cplusplus
}
#endif

#endif
