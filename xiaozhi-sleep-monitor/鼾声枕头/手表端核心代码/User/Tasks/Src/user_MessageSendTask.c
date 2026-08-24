/* Private includes -----------------------------------------------------------*/
//includes
#include "string.h"
#include "stdio.h"

#include "main.h"
#include "stm32f4xx_it.h"
#include "rtc.h"

#include "user_TasksInit.h"
#include "user_MessageSendTask.h"

#include "ui.h"
#include "ui_EnvPage.h"
#include "ui_HRPage.h"
#include "ui_SPO2Page.h"
#include "ui_HomePage.h"
#include "ui_DateTimeSetPage.h"

#include "HWDataAccess.h"
#include "MPU6050.h"
#include "SleepAlgorithm.h"
#include "user_SleepMonitorTask.h"
#include "version.h"

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
struct
{
	RTC_DateTypeDef nowdate;
	RTC_TimeTypeDef nowtime;
	int8_t humi;
	int8_t temp;
	uint8_t HR;
	uint8_t SPO2;
	uint16_t stepNum;
	uint8_t posture;
}BLEMessage;

struct
{
	RTC_DateTypeDef nowdate;
	RTC_TimeTypeDef nowtime;
}TimeSetMessage;

/* Private function prototypes -----------------------------------------------*/

void StrCMD_Get(uint8_t * str,uint8_t * cmd)
{
	uint8_t i=0;
  while(str[i]!='=')
  {
      cmd[i] = str[i];
      i++;
  }
}

//set time//OV+ST=20230629125555
uint8_t TimeFormat_Get(uint8_t * str)
{
	TimeSetMessage.nowdate.Year = (str[8]-'0')*10+str[9]-'0';
	TimeSetMessage.nowdate.Month = (str[10]-'0')*10+str[11]-'0';
	TimeSetMessage.nowdate.Date = (str[12]-'0')*10+str[13]-'0';
	TimeSetMessage.nowtime.Hours = (str[14]-'0')*10+str[15]-'0';
	TimeSetMessage.nowtime.Minutes = (str[16]-'0')*10+str[17]-'0';
	TimeSetMessage.nowtime.Seconds = (str[18]-'0')*10+str[19]-'0';
	if(TimeSetMessage.nowdate.Year>0 && TimeSetMessage.nowdate.Year<99
		&& TimeSetMessage.nowdate.Month>0 && TimeSetMessage.nowdate.Month<=12
		&& TimeSetMessage.nowdate.Date>0 && TimeSetMessage.nowdate.Date<=31
		&& TimeSetMessage.nowtime.Hours>=0 && TimeSetMessage.nowtime.Hours<=23
		&& TimeSetMessage.nowtime.Minutes>=0 && TimeSetMessage.nowtime.Minutes<=59
		&& TimeSetMessage.nowtime.Seconds>=0 && TimeSetMessage.nowtime.Seconds<=59)
	{
		RTC_SetDate(TimeSetMessage.nowdate.Year, TimeSetMessage.nowdate.Month,TimeSetMessage.nowdate.Date);
		RTC_SetTime(TimeSetMessage.nowtime.Hours,TimeSetMessage.nowtime.Minutes,TimeSetMessage.nowtime.Seconds);
		printf("{\"status\":\"TIMESET_OK\"}\r\n");
	}
	return 0;
}

/**
  * @brief  send the message via BLE, use uart
  * @param  argument: Not used
  * @retval None
  */
void MessageSendTask(void *argument)
{
	while(1)
	{
		uint32_t hardint_flags = osEventFlagsWait(HardIntEventHandle, HARDINT_EVENT_UART, osFlagsWaitAny, osWaitForever);
		if((hardint_flags & HARDINT_EVENT_UART) != 0U)
		{
			uint8_t IdleBreakstr = 0;
			osMessageQueuePut(IdleBreak_MessageQueue,&IdleBreakstr,NULL,1);

			if(!strcmp(HardInt_receive_str,"OV"))
			{
				printf("{\"status\":\"OK\"}\r\n");
			}
			else if(!strcmp(HardInt_receive_str,"OV+VERSION"))
			{
				printf("{\"version\":\"V%d.%d.%d\",\"info\":\"%s\"}\r\n",
				       VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_INFO);
			}
			else if(!strcmp(HardInt_receive_str,"OV+SEND"))
			{
				HAL_RTC_GetTime(&hrtc,&(BLEMessage.nowtime),RTC_FORMAT_BIN);
				HAL_RTC_GetDate(&hrtc,&BLEMessage.nowdate,RTC_FORMAT_BIN);
				BLEMessage.humi = HWInterface.AHT21.humidity;
				BLEMessage.temp = HWInterface.AHT21.temperature;
				BLEMessage.HR = HWInterface.HR_meter.HrRate;
				BLEMessage.SPO2 = HWInterface.HR_meter.SPO2;
				BLEMessage.stepNum = HWInterface.IMU.Steps;
				BLEMessage.posture = MPU_Get_SleepPosture();

				printf("{\"date\":\"%02d-%02d\",\"time\":\"%02d:%02d:%02d\","
				       "\"humi\":%d,\"temp\":%d,\"hr\":%d,\"spo2\":%d,\"spo2_ref\":1,\"step\":%d,\"posture\":%d}\r\n",
				       BLEMessage.nowdate.Month, BLEMessage.nowdate.Date,
				       BLEMessage.nowtime.Hours, BLEMessage.nowtime.Minutes, BLEMessage.nowtime.Seconds,
				       BLEMessage.humi, BLEMessage.temp, BLEMessage.HR, BLEMessage.SPO2,
				       BLEMessage.stepNum, BLEMessage.posture);
			}
			else if(!strcmp(HardInt_receive_str,"OV+POSTURE"))
			{
				uint8_t p = MPU_Get_SleepPosture();
				printf("{\"posture\":%d,\"name\":\"%s\"}\r\n", p, MPU_Get_PostureName(p));
			}
			// ←── 震动马达控制 ──→
			else if(!strcmp(HardInt_receive_str,"OV+MOTOR=SHORT"))
			{
				HWInterface.Motor.ShortVib();
				printf("{\"motor\":\"SHORT\",\"status\":\"OK\"}\r\n");
			}
			else if(!strcmp(HardInt_receive_str,"OV+MOTOR=LONG"))
			{
				HWInterface.Motor.LongVib();
				printf("{\"motor\":\"LONG\",\"status\":\"OK\"}\r\n");
			}
			else if(!strcmp(HardInt_receive_str,"OV+MOTOR=DOUBLE"))
			{
				HWInterface.Motor.DoubleVib();
				printf("{\"motor\":\"DOUBLE\",\"status\":\"OK\"}\r\n");
			}
			else if(!strcmp(HardInt_receive_str,"OV+MOTOR=START"))
			{
				HWInterface.Motor.Start();
				printf("{\"motor\":\"START\",\"status\":\"OK\"}\r\n");
			}
			else if(!strcmp(HardInt_receive_str,"OV+MOTOR=STOP"))
			{
				HWInterface.Motor.Stop();
				printf("{\"motor\":\"STOP\",\"status\":\"OK\"}\r\n");
			}
			else if(!strcmp(HardInt_receive_str,"OV+IMU"))
			{
				short ax = 0, ay = 0, az = 0;
				uint8_t motion = 0;
				if (!HWInterface.IMU.ConnectionError) {
					MPU_Get_Accelerometer(&ax, &ay, &az);
					motion = MPU_Get_MotionStatus(&ax, &ay, &az);
				}
				printf("{\"ax\":%d,\"ay\":%d,\"az\":%d,\"motion\":%d}\r\n", ax, ay, az, motion);
			}
			else if(!strcmp(HardInt_receive_str,"OV+SLEEPDATA"))
			{
				if (SleepMonitor_IsActive()) {
					uint16_t ec = SleepMonitor_GetEpochCount();
					const SleepEpoch_t *epochs = SleepMonitor_GetEpochs();
					const SleepSummary_t *sum = SleepMonitor_GetSummary();
					if (ec > 0) {
						const SleepEpoch_t *ep = &epochs[ec - 1];
						printf("{\"active\":1,\"epoch\":%d,\"stage\":%d,\"hr\":%d,"
						       "\"motion\":%lu,\"posture\":%d,\"changes\":%d,"
						       "\"hrv\":%d,\"breath\":%d,\"valid\":%d}\r\n",
						       ec, ep->stage, ep->avg_hr,
						       (unsigned long)ep->motion_index, ep->posture, ep->posture_changes,
						       ep->hrv_rmssd, ep->breath_rate, ep->valid);
					} else {
						printf("{\"active\":1,\"epoch\":0}\r\n");
					}
				} else {
					printf("{\"active\":0}\r\n");
				}
			}
			//set time//OV+ST=20230629125555
			else if(strlen(HardInt_receive_str)==20)
			{
				uint8_t cmd[10];
				memset(cmd,0,sizeof(cmd));
				StrCMD_Get(HardInt_receive_str,cmd);
				if(ui_APPSy_EN && !strcmp(cmd,"OV+ST"))
				{
					TimeFormat_Get(HardInt_receive_str);
				}
			}
			else
			{
				printf("{\"status\":\"UNKNOWN_CMD\"}\r\n");
			}
			memset(HardInt_receive_str,0,sizeof(HardInt_receive_str));
		}
	}
}
