#ifndef __MOTOR_H__
#define __MOTOR_H__

#include "main.h"

/* PB15 = LEDEN, 控制震动马达 N-MOSFET 栅极, 高电平导通 */
#define MOTOR_PORT              GPIOB
#define MOTOR_PIN               GPIO_PIN_15

#define MOTOR_ON()              HAL_GPIO_WritePin(MOTOR_PORT, MOTOR_PIN, GPIO_PIN_SET)
#define MOTOR_OFF()             HAL_GPIO_WritePin(MOTOR_PORT, MOTOR_PIN, GPIO_PIN_RESET)

/* 震动模式 */
typedef enum {
    MOTOR_SHORT  = 0,   /* 短震 100ms */
    MOTOR_LONG   = 1,   /* 长震 500ms */
    MOTOR_DOUBLE = 2,   /* 双震 100ms × 2 */
} MotorPattern_t;

void Motor_GPIO_Init(void);
void Motor_Vibrate(MotorPattern_t pattern);

/* 连续震动控制（用于蓝牙 MQTT 远程控制） */
void Motor_ContinuousStart(void);
void Motor_ContinuousStop(void);

#endif
