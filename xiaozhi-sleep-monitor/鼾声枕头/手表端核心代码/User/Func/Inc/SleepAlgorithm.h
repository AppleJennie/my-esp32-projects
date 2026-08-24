#ifndef __SLEEP_ALGORITHM_H__
#define __SLEEP_ALGORITHM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* Sleep stages */
#define SLEEP_STAGE_WAKE      0
#define SLEEP_STAGE_REM       1
#define SLEEP_STAGE_LIGHT     2
#define SLEEP_STAGE_DEEP      3
#define SLEEP_STAGE_UNKNOWN   4

/* Epoch config: 30 seconds per epoch */
#define SLEEP_EPOCH_SEC           30
#define SLEEP_PPG_SAMPLE_HZ       20
#define SLEEP_ACC_SAMPLE_HZ       20
#define SLEEP_EPOCH_PPG_SAMPLES   (SLEEP_EPOCH_SEC * SLEEP_PPG_SAMPLE_HZ)   /* 600 */
#define SLEEP_EPOCH_ACC_SAMPLES   (SLEEP_EPOCH_SEC * SLEEP_ACC_SAMPLE_HZ)   /* 600 */

/* HRV queue depth for RMSSD/SDNN (last 5 minutes = 10 epochs) */
#define HRV_EPOCH_QUEUE_LEN       10

/* RR interval queue for instant HRV (about 5 minutes of beats) */
#define RR_QUEUE_LEN              64

/* Sleep epoch result structure */
typedef struct {
    uint32_t start_timestamp;   /* Epoch start time in seconds */
    uint8_t  stage;             /* SLEEP_STAGE_XXX */
    uint8_t  avg_hr;            /* Average HR in this epoch (BPM) */
    uint8_t  min_hr;            /* Minimum HR in this epoch */
    uint8_t  max_hr;            /* Maximum HR in this epoch */
    uint16_t hr_std;            /* HR standard deviation (x10, 0-65535) */
    uint16_t hrv_rmssd;         /* RMSSD in ms (0-65535) */
    uint32_t motion_index;      /* Accumulated motion intensity */
    uint8_t  posture;           /* Dominant posture (POSTURE_XXX) */
    uint8_t  posture_changes;   /* Posture changes in this epoch */
    uint8_t  breath_rate;       /* Estimated breath rate (BPM) */
    uint8_t  valid;             /* 1=valid data, 0=not worn/invalid */
} SleepEpoch_t;

/* Night summary */
typedef struct {
    uint8_t  month;
    uint8_t  day;
    uint16_t total_min;      /* Total sleep time (minutes) */
    uint8_t  deep_min;       /* Deep sleep minutes */
    uint8_t  light_min;      /* Light sleep minutes */
    uint8_t  rem_min;        /* REM minutes */
    uint8_t  awake_min;      /* Awake minutes */
    uint8_t  avg_hr;         /* Average HR */
    uint8_t  min_hr;         /* Minimum HR */
    uint16_t avg_hrv;        /* Average RMSSD */
    uint8_t  breath_rate;    /* Average breath rate */
    uint8_t  posture_changes;/* Total posture changes */
    uint8_t  sleep_score;    /* 0-100 */
    uint8_t  fall_asleep_min;/* Minutes to fall asleep */
    /* Posture duration stats (minutes) */
    uint8_t  supine_min;     /* Time spent supine (仰卧) */
    uint8_t  left_min;       /* Time spent on left side (左侧卧) */
    uint8_t  right_min;      /* Time spent on right side (右侧卧) */
    uint8_t  prone_min;      /* Time spent prone (俯卧) */
} SleepSummary_t;

/* Public API */
void SleepAlgo_Init(void);

/* Called every PPG sample (e.g. 25Hz, every 40ms) */
void SleepAlgo_FeedPPG(uint16_t ppg_val, uint32_t tick_ms);

/* Called every accelerometer sample (e.g. 25Hz, every 40ms) */
void SleepAlgo_FeedAccel(short ax, short ay, short az);

/* Called periodically for posture (e.g. every 1 second) */
void SleepAlgo_FeedPosture(uint8_t posture);

/* Called every epoch (30s) to finalize current epoch and start a new one */
bool SleepAlgo_FinishEpoch(SleepEpoch_t *epoch_out);

/* Get current instant HR (BPM), 0 if not ready */
uint8_t SleepAlgo_GetCurrentHR(void);

/* Get current instant HRV (RMSSD in ms), 0 if not ready */
uint16_t SleepAlgo_GetCurrentHRV(void);

/* Get current posture (POSTURE_XXX), POSTURE_UNKNOWN if not ready */
uint8_t SleepAlgo_GetCurrentPosture(void);

/* Get posture changes in current epoch */
uint8_t SleepAlgo_GetCurrentPostureChanges(void);

/* Calculate sleep score from summary */
uint8_t SleepAlgo_CalculateScore(const SleepSummary_t *sum);

/* Classify a single epoch (can be called externally for testing) */
uint8_t SleepAlgo_ClassifyEpoch(const SleepEpoch_t *epoch);

/* Smooth stages with 3-epoch window to reduce jitter */
void SleepAlgo_SmoothStages(SleepEpoch_t *epochs, uint16_t count);

/* Build summary from epoch array */
void SleepAlgo_BuildSummary(const SleepEpoch_t *epochs, uint16_t count, SleepSummary_t *sum);

#ifdef __cplusplus
}
#endif

#endif /* __SLEEP_ALGORITHM_H__ */
