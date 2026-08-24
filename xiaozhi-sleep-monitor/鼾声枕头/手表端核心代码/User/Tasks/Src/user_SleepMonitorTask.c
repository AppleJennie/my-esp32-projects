/* Private includes -----------------------------------------------------------*/
#include "user_SleepMonitorTask.h"
#include "user_TasksInit.h"
#include "HWDataAccess.h"

#include "em70x8.h"
#include "MPU6050.h"
#include "lcd_init.h"
#include "rtc.h"
#include "CST816.h"
#include "ui_HomePage.h"

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/
#define SLEEP_SAMPLE_PERIOD_MS    50   /* 20Hz, matching original HR task */
#define SLEEP_ACC_DIVIDER         1    /* Read ACC every sample (50ms) */
#define SLEEP_EPOCH_TICKS         (SLEEP_EPOCH_SEC * 1000 / SLEEP_SAMPLE_PERIOD_MS) /* 600 */

/* Auto-monitor time window: 22:00 ~ 08:00 */
#define SLEEP_AUTO_START_HOUR     22
#define SLEEP_AUTO_END_HOUR       8
#define SLEEP_AUTO_CHECK_MS       60000 /* Check time every 60 seconds */

/* Private variables ---------------------------------------------------------*/
volatile uint8_t SleepMonitor_State = SLEEP_MON_STATE_IDLE;

static SleepEpoch_t s_epochs[SLEEP_MAX_EPOCHS];
static uint16_t s_epoch_count = 0;
static SleepSummary_t s_summary;
static uint8_t s_not_worn_epochs = 0;
static uint16_t s_total_posture_changes = 0;

/* Message queue for sleep commands */
osMessageQueueId_t SleepMon_MessageQueue;

/* Private function prototypes -----------------------------------------------*/
static void sleep_monitor_begin(void);
static void sleep_monitor_end(void);
static void sleep_monitor_sample(void);
static void sleep_monitor_process_epoch(void);
static void sleep_monitor_check_auto_time(void);

/* Tasks ---------------------------------------------------------------------*/

void SleepMonitorTask(void *argument)
{
    uint8_t cmd = 0;
    uint32_t sample_tick = 0;
    uint16_t epoch_tick = 0;
    uint32_t auto_check_tick = 0;

    while (1) {
        /* Process manual commands (non-blocking) */
        if (osMessageQueueGet(SleepMon_MessageQueue, &cmd, NULL, 0) == osOK) {
            if (cmd == 1 && SleepMonitor_State == SLEEP_MON_STATE_IDLE) {
                sleep_monitor_begin();
            } else if (cmd == 2 && SleepMonitor_State == SLEEP_MON_STATE_RUNNING) {
                sleep_monitor_end();
            }
        }

        /* Auto time-based start/stop */
        uint32_t now = HAL_GetTick();
        if ((now - auto_check_tick) >= SLEEP_AUTO_CHECK_MS) {
            auto_check_tick = now;
            sleep_monitor_check_auto_time();
        }

        if (SleepMonitor_State == SLEEP_MON_STATE_RUNNING) {
            if ((now - sample_tick) >= SLEEP_SAMPLE_PERIOD_MS) {
                sample_tick = now;
                sleep_monitor_sample();
                epoch_tick++;

                if (epoch_tick >= SLEEP_EPOCH_TICKS) {
                    epoch_tick = 0;
                    sleep_monitor_process_epoch();
                }
            }
        }

        osDelay(5); /* Fast poll for precise timing */
    }
}

/* ======================= Internal functions ======================= */

static void sleep_monitor_begin(void)
{
    SleepAlgo_Init();
    memset(s_epochs, 0, sizeof(s_epochs));
    memset(&s_summary, 0, sizeof(s_summary));
    s_epoch_count = 0;
    s_not_worn_epochs = 0;
    s_total_posture_changes = 0;

    /* Wake up sensors */
    EM7028_hrs_Enable();
    MPU_Wakeup();

    SleepMonitor_State = SLEEP_MON_STATE_RUNNING;
}

static void sleep_monitor_end(void)
{
    SleepMonitor_State = SLEEP_MON_STATE_IDLE;

    /* Disable HR sensor to save power */
    EM7028_hrs_DisEnable();

    /* Smooth stages for better real-time display */
    SleepAlgo_SmoothStages(s_epochs, s_epoch_count);

    /* Build summary in RAM for BLE query (not saved to EEPROM) */
    SleepAlgo_BuildSummary(s_epochs, s_epoch_count, &s_summary);

    /* Get current date */
    HW_DateTimeTypeDef dt;
    HWInterface.RealTimeClock.GetTimeDate(&dt);
    s_summary.month = dt.Month;
    s_summary.day = dt.Date;

}

static void sleep_monitor_sample(void)
{
    uint16_t ppg = 0;
    short ax = 0, ay = 0, az = 0;

    /* Combine sensor reads into one critical section to reduce scheduler overhead */
    vTaskSuspendAll();
    if (!HWInterface.HR_meter.ConnectionError) {
        ppg = EM7028_Get_HRS1();
    }
    if (!HWInterface.IMU.ConnectionError) {
        MPU_Get_Accelerometer(&ax, &ay, &az);
    }
    xTaskResumeAll();

    /* Feed algorithm */
    SleepAlgo_FeedPPG(ppg, HAL_GetTick());
    SleepAlgo_FeedAccel(ax, ay, az);

    /* Posture detection every ~1 second (20 samples at 20Hz) */
    static uint8_t posture_cnt = 0;
    posture_cnt++;
    if (posture_cnt >= 20) {
        posture_cnt = 0;
        uint8_t p = MPU_Get_SleepPosture();
        SleepAlgo_FeedPosture(p);
    }
}

static void sleep_monitor_process_epoch(void)
{
    if (s_epoch_count >= SLEEP_MAX_EPOCHS) {
        /* Buffer full, stop monitoring */
        sleep_monitor_end();
        return;
    }

    SleepEpoch_t *ep = &s_epochs[s_epoch_count];
    if (SleepAlgo_FinishEpoch(ep)) {
        ep->start_timestamp = s_epoch_count * SLEEP_EPOCH_SEC;

        /* Not-worn detection: if no valid HR for 2 consecutive epochs, pause */
        if (!ep->valid) {
            s_not_worn_epochs++;
            if (s_not_worn_epochs >= NOT_WORN_EPOCH_THRESHOLD) {
                /* Mark as unknown, but keep monitoring */
                ep->stage = SLEEP_STAGE_UNKNOWN;
            }
        } else {
            s_not_worn_epochs = 0;
        }

        s_total_posture_changes += ep->posture_changes;
        s_epoch_count++;
    }
}

static void sleep_monitor_check_auto_time(void)
{
    RTC_TimeTypeDef nowtime;
    RTC_DateTypeDef nowdate;
    HAL_RTC_GetTime(&hrtc, &nowtime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &nowdate, RTC_FORMAT_BIN);

    uint8_t hour = nowtime.Hours;
    /* Night window: 22:00 ~ 08:00 */
    uint8_t is_night = (hour >= SLEEP_AUTO_START_HOUR || hour < SLEEP_AUTO_END_HOUR);

    if (is_night && SleepMonitor_State == SLEEP_MON_STATE_IDLE) {
        sleep_monitor_begin();
    } else if (!is_night && SleepMonitor_State == SLEEP_MON_STATE_RUNNING) {
        sleep_monitor_end();
    }
}

/* ======================= Public control functions ======================= */

void SleepMonitor_Start(void)
{
    uint8_t cmd = 1;
    osMessageQueuePut(SleepMon_MessageQueue, &cmd, 0, 100);
}

void SleepMonitor_Stop(void)
{
    uint8_t cmd = 2;
    osMessageQueuePut(SleepMon_MessageQueue, &cmd, 0, 100);
}

uint16_t SleepMonitor_GetEpochCount(void)
{
    return s_epoch_count;
}

const SleepEpoch_t *SleepMonitor_GetEpochs(void)
{
    return s_epochs;
}

const SleepSummary_t *SleepMonitor_GetSummary(void)
{
    return &s_summary;
}

uint16_t SleepMonitor_GetTotalPostureChanges(void)
{
    if (SleepMonitor_State == SLEEP_MON_STATE_RUNNING) {
        return s_total_posture_changes + SleepAlgo_GetCurrentPostureChanges();
    }
    return s_summary.posture_changes;
}
