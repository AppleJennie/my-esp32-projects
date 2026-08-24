#include "SportManager.h"
#include "HWDataAccess.h"
#include "DataSave.h"
#include "string.h"

/************* globals *************/
SportNow_t     SportNow;
SportSetting_t SportSetting;
uint8_t        SportTodayMin = 0;
uint8_t        SportTodayKcal = 0;

/************* private *************/
static uint32_t hr_sum = 0;
static uint16_t hr_cnt = 0;
static uint32_t kcal_acc = 0;      // sum of weight*met_x2 every 500ms
static uint16_t ms_acc = 0;

// MET*2: run 8.0, walk 3.5, ride 6.0
static const uint8_t sport_met_x2[SPORT_TYPE_NUM] = {16, 7, 12};

#define SPORT_REC_HEAD_ADDR   0x40
#define SPORT_REC_BASE_ADDR   0x41

static void Sport_SettingDefault(void)
{
    SportSetting.weight_kg = 60;
    SportSetting.step_goal = 8000;
    SportSetting.sport_goal_min = 30;
    SportSetting.remind_hour = 18;
    SportSetting.remind_min = 30;
    SportSetting.sedentary_en = 1;
}

static void Sport_SaveToday(void)
{
    HW_DateTimeTypeDef nowdatetime;
    uint8_t dat[3];
    HWInterface.RealTimeClock.GetTimeDate(&nowdatetime);
    dat[0] = nowdatetime.Date;
    dat[1] = SportTodayMin;
    dat[2] = SportTodayKcal;
    SettingSave(dat, 0x38, 3);
}

/**
  * @brief  load settings and today data from EEPROM
  * @retval None
  */
void Sport_Init(void)
{
    uint8_t dat[7];

    SportNow.state = SPORT_IDLE;
    SportNow.type = SPORT_RUN;

    if(SettingGet(dat, 0x30, 7) == 0)
    {
        SportSetting.weight_kg = dat[0];
        SportSetting.step_goal = (uint16_t)dat[2] << 8 | dat[1];
        SportSetting.sport_goal_min = dat[3];
        SportSetting.remind_hour = dat[4];
        SportSetting.remind_min = dat[5];
        SportSetting.sedentary_en = dat[6];
        if(SportSetting.weight_kg < 20 || SportSetting.weight_kg > 200 ||
           SportSetting.step_goal == 0 ||
           SportSetting.remind_hour > 23 || SportSetting.remind_min > 59 ||
           SportSetting.sedentary_en > 1)
        {
            Sport_SettingDefault();
        }
    }
    else
    {
        Sport_SettingDefault();
    }

    // today sport data, clear when the date changed
    SportTodayMin = 0;
    SportTodayKcal = 0;
    if(SettingGet(dat, 0x38, 3) == 0)
    {
        HW_DateTimeTypeDef nowdatetime;
        HWInterface.RealTimeClock.GetTimeDate(&nowdatetime);
        if(dat[0] == nowdatetime.Date)
        {
            SportTodayMin = dat[1];
            SportTodayKcal = dat[2];
        }
    }
}

/**
  * @brief  save settings to EEPROM
  * @retval None
  */
void Sport_SaveSetting(void)
{
    uint8_t dat[7];
    dat[0] = SportSetting.weight_kg;
    dat[1] = SportSetting.step_goal & 0xff;
    dat[2] = SportSetting.step_goal >> 8 & 0xff;
    dat[3] = SportSetting.sport_goal_min;
    dat[4] = SportSetting.remind_hour;
    dat[5] = SportSetting.remind_min;
    dat[6] = SportSetting.sedentary_en;
    SettingSave(dat, 0x30, 7);
}

/**
  * @brief  clear today data, called by DataSaveTask when the date changed
  * @retval None
  */
void Sport_OnDateChange(void)
{
    SportTodayMin = 0;
    SportTodayKcal = 0;
    Sport_SaveToday();
}

/**
  * @brief  start a sport session
  * @param  type: SPORT_RUN / SPORT_WALK / SPORT_RIDE
  * @retval None
  */
void Sport_Start(SportType_t type)
{
    if(type >= SPORT_TYPE_NUM)
        type = SPORT_RUN;
    memset(&SportNow, 0, sizeof(SportNow));
    SportNow.type = type;
    SportNow.state = SPORT_RUNNING;
    SportNow.start_steps = HWInterface.IMU.Steps;
    hr_sum = 0;
    hr_cnt = 0;
    kcal_acc = 0;
    ms_acc = 0;
}

void Sport_Pause(void)
{
    if(SportNow.state == SPORT_RUNNING)
        SportNow.state = SPORT_PAUSED;
}

void Sport_Resume(void)
{
    if(SportNow.state == SPORT_PAUSED)
        SportNow.state = SPORT_RUNNING;
}

/**
  * @brief  stop the session, save a record and the today data
  * @retval None
  */
void Sport_Stop(void)
{
    SportRecord_t rec;
    uint8_t dat[8];
    uint8_t index = 0;

    if(SportNow.state == SPORT_IDLE)
        return;
    SportNow.state = SPORT_IDLE;

    // HR sensor back to sleep
    if(!HWInterface.HR_meter.ConnectionError)
        HWInterface.HR_meter.Sleep();

    // today data
    uint16_t min = SportTodayMin + SportNow.duration_s / 60;
    SportTodayMin = min > 255 ? 255 : (uint8_t)min;
    min = SportTodayKcal + SportNow.kcal;
    SportTodayKcal = min > 255 ? 255 : (uint8_t)min;
    Sport_SaveToday();

    // do not save a record shorter than 1 minute
    if(SportNow.duration_s < 60)
        return;

    HW_DateTimeTypeDef nowdatetime;
    HWInterface.RealTimeClock.GetTimeDate(&nowdatetime);
    rec.type = (uint8_t)SportNow.type;
    rec.month = nowdatetime.Month;
    rec.date = nowdatetime.Date;
    rec.duration_min = SportNow.duration_s / 60 > 255 ? 255 : (uint8_t)(SportNow.duration_s / 60);
    rec.steps = SportNow.steps;
    rec.avg_hr = SportNow.avg_hr;
    rec.kcal = SportNow.kcal > 255 ? 255 : (uint8_t)SportNow.kcal;

    memcpy(dat, &rec, 8);
    if(SettingGet(&index, SPORT_REC_HEAD_ADDR, 1) != 0 || index >= SPORT_RECORD_NUM)
        index = 0;
    SettingSave(dat, SPORT_REC_BASE_ADDR + index * 8, 8);
    index = (index + 1) % SPORT_RECORD_NUM;
    SettingSave(&index, SPORT_REC_HEAD_ADDR, 1);
}

/**
  * @brief  update the session data, call every 500ms
  * @retval None
  */
void Sport_Update500ms(void)
{
    if(SportNow.state != SPORT_RUNNING)
        return;

    // duration
    ms_acc += 500;
    SportNow.duration_s += ms_acc / 1000;
    ms_acc %= 1000;

    // steps during this session
    SportNow.steps = HWInterface.IMU.Steps - SportNow.start_steps;

    // heart rate
    SportNow.hr = HWInterface.HR_meter.HrRate;
    if(SportNow.hr >= 40 && SportNow.hr <= 200)
    {
        hr_sum += SportNow.hr;
        hr_cnt++;
        SportNow.avg_hr = (uint8_t)(hr_sum / hr_cnt);
    }

    // kcal = weight * MET * hours
    kcal_acc += (uint32_t)SportSetting.weight_kg * sport_met_x2[SportNow.type];
    SportNow.kcal = (uint16_t)(kcal_acc / 144 / 100);
}

/**
  * @brief  read a history record
  * @param  n: 0 is the latest
  * @param  rec: record output
  * @retval 1: valid, 0: invalid
  */
uint8_t Sport_GetRecord(uint8_t n, SportRecord_t *rec)
{
    uint8_t index = 0;
    uint8_t dat[8];

    if(n >= SPORT_RECORD_NUM)
        return 0;
    if(SettingGet(&index, SPORT_REC_HEAD_ADDR, 1) != 0 || index >= SPORT_RECORD_NUM)
        return 0;
    index = (index + SPORT_RECORD_NUM - 1 - n) % SPORT_RECORD_NUM;
    if(SettingGet(dat, SPORT_REC_BASE_ADDR + index * 8, 8) != 0)
        return 0;
    memcpy(rec, dat, 8);
    if(rec->month < 1 || rec->month > 12 || rec->type >= SPORT_TYPE_NUM)
        return 0;
    return 1;
}

/**
  * @brief  HR zone, max HR = 190 (age 30)
  * @param  hr: heart rate
  * @retval 0 warm up, 1 fat burn, 2 cardio, 3 peak
  */
uint8_t Sport_HRZone(uint8_t hr)
{
    if(hr < 114)      // <60%
        return 0;
    else if(hr < 133) // 60%-70%
        return 1;
    else if(hr < 162) // 70%-85%
        return 2;
    else
        return 3;
}
