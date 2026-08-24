#ifndef R60ABD1_ADAPTER_H
#define R60ABD1_ADAPTER_H
#include "fusion_types.h"
#include "sleep_radar_data.h"
#ifdef __cplusplus
extern "C" {
#endif

/* 将 R60ABD1 雷达原始数据快照转换为融合层统一 radar_feature_t */
void r60abd1_adapter_convert(const sleep_radar_data_t *radar, radar_feature_t *out);

#ifdef __cplusplus
}
#endif
#endif
