#ifndef __SLEEP_RECORD_H__
#define __SLEEP_RECORD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "SleepAlgorithm.h"

/* EEPROM layout for sleep records (BL24C02 = 256 bytes)
 * Base addresses:
 *   0x00-0x01: Magic 0x55 0xAA
 *   0x10-0x11: Settings
 *   0x20-0x22: Date + steps
 *   0x30: Sleep record index head
 *   0x31: Sleep record count (0-10)
 *   0x32-0x33: Reserved
 *   0x34+: Records, each 16 bytes
 *   Max records: 10 (0x34 to 0xD3)
 */

#define SLEEP_RECORD_ADDR_HEAD      0x30
#define SLEEP_RECORD_ADDR_COUNT     0x31
#define SLEEP_RECORD_ADDR_BASE      0x34
#define SLEEP_RECORD_MAX_COUNT      10
#define SLEEP_RECORD_SIZE           16

/* Compressed sleep record for EEPROM (16 bytes) */
typedef struct {
    uint8_t  month;          /* 1-12 */
    uint8_t  day;            /* 1-31 */
    uint16_t total_min;      /* Total sleep minutes */
    uint8_t  deep_min;       /* Deep sleep */
    uint8_t  light_min;      /* Light sleep */
    uint8_t  rem_min;        /* REM */
    uint8_t  awake_min;      /* Awake */
    uint8_t  avg_hr;         /* Average HR */
    uint8_t  min_hr;         /* Minimum HR */
    uint8_t  sleep_score;    /* 0-100 */
    uint8_t  posture_chg;    /* Posture changes */
    uint8_t  fall_asleep_min;/* Minutes to fall asleep */
    uint8_t  reserved;       /* Reserved / breath rate if needed */
    uint8_t  checksum;       /* Simple checksum */
} SleepRecord_Entry_t;

void SleepRecord_Init(void);

/* Save a sleep summary to EEPROM. Returns true on success. */
bool SleepRecord_Save(const SleepSummary_t *summary);

/* Load the most recent record into summary. Returns true on success. */
bool SleepRecord_LoadLatest(SleepSummary_t *summary);

/* Load a specific record by index (0 = most recent). Returns true on success. */
bool SleepRecord_LoadByIndex(uint8_t idx, SleepSummary_t *summary);

/* Get total stored record count (0-10) */
uint8_t SleepRecord_GetCount(void);

/* Clear all sleep records */
void SleepRecord_Clear(void);

#ifdef __cplusplus
}
#endif

#endif /* __SLEEP_RECORD_H__ */
