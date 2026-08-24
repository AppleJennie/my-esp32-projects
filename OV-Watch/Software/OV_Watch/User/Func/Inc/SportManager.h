#ifndef _SPORTMANAGER_H
#define _SPORTMANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stdint.h"

/******************************************
sport manager: sport mode record, kcal,
history record in EEPROM, user settings

EEPROM Data description:
[0x30]:weight_kg
[0x31-0x32]:step_goal (little endian)
[0x33]:sport_goal_min
[0x34]:remind_hour
[0x35]:remind_min
[0x36]:sedentary_en

[0x38]:today date
[0x39]:today sport minutes
[0x3A]:today kcal

[0x40]:record ring write index(0-15)
[0x41-0xC0]:16 * 8Bytes sport records
*******************************************/

/************* sport type & state *************/
typedef enum
{
    SPORT_RUN = 0,
    SPORT_WALK,
    SPORT_RIDE,
    SPORT_TYPE_NUM
} SportType_t;

typedef enum
{
    SPORT_IDLE = 0,
    SPORT_RUNNING,
    SPORT_PAUSED
} SportState_t;

/************* current session *************/
typedef struct
{
    SportState_t state;
    SportType_t  type;
    uint32_t duration_s;   // running time in seconds
    uint16_t start_steps;  // pedometer value at start
    uint16_t steps;        // steps during this session
    uint8_t  hr;           // latest heart rate
    uint8_t  avg_hr;
    uint16_t kcal;
} SportNow_t;

/************* user settings *************/
typedef struct
{
    uint8_t  weight_kg;      // for kcal calculate, default 60
    uint16_t step_goal;      // daily step goal, default 8000
    uint8_t  sport_goal_min; // daily sport minutes goal, default 30
    uint8_t  remind_hour;    // sport remind time, default 18:30
    uint8_t  remind_min;
    uint8_t  sedentary_en;   // sedentary remind switch, default 1
} SportSetting_t;

/************* history record, 8 bytes in EEPROM *************/
typedef struct
{
    uint8_t type;         // SportType_t
    uint8_t month;
    uint8_t date;
    uint8_t duration_min;
    uint16_t steps;
    uint8_t avg_hr;
    uint8_t kcal;         // capped at 255
} SportRecord_t;

#define SPORT_RECORD_NUM  16

extern SportNow_t     SportNow;
extern SportSetting_t SportSetting;
extern uint8_t        SportTodayMin;   // today sport minutes
extern uint8_t        SportTodayKcal;  // today sport kcal (capped 255)

void Sport_Init(void);
void Sport_SaveSetting(void);
void Sport_OnDateChange(void);

void Sport_Start(SportType_t type);
void Sport_Pause(void);
void Sport_Resume(void);
void Sport_Stop(void);

// call every 500ms from SensorDataUpdateTask
void Sport_Update500ms(void);

// history: n=0 is the latest, return 1 when the record is valid
uint8_t Sport_GetRecord(uint8_t n, SportRecord_t *rec);

// HR zone: 0 warm up, 1 fat burn, 2 cardio, 3 peak
uint8_t Sport_HRZone(uint8_t hr);

#ifdef __cplusplus
}
#endif

#endif
