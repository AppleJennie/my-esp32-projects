#ifndef __USER_SLEEP_MONITOR_TASK_H__
#define __USER_SLEEP_MONITOR_TASK_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "SleepAlgorithm.h"

/* Sleep monitor states */
#define SLEEP_MON_STATE_IDLE      0
#define SLEEP_MON_STATE_RUNNING   1
#define SLEEP_MON_STATE_PAUSED    2

/* Max epochs for one night: 8 hours = 960 epochs */
#define SLEEP_MAX_EPOCHS          960

/* Inactivity threshold to detect "not worn" (no PPG peaks) in epochs */
#define NOT_WORN_EPOCH_THRESHOLD  2

extern volatile uint8_t SleepMonitor_State;
/* Start sleep monitoring */
void SleepMonitor_Start(void);

/* Stop sleep monitoring and save result */
void SleepMonitor_Stop(void);

/* Check if currently monitoring */
static inline bool SleepMonitor_IsActive(void) {
    return (SleepMonitor_State == SLEEP_MON_STATE_RUNNING);
}

/* Get current night epoch count */
uint16_t SleepMonitor_GetEpochCount(void);

/* Get epoch array pointer (read-only, for UI) */
const SleepEpoch_t *SleepMonitor_GetEpochs(void);

/* Get latest summary (valid after stop) */
const SleepSummary_t *SleepMonitor_GetSummary(void);

/* Get total posture changes so far (current epoch included) */
uint16_t SleepMonitor_GetTotalPostureChanges(void);

/* Task function */
void SleepMonitorTask(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* __USER_SLEEP_MONITOR_TASK_H__ */
