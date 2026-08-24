#include "motor.h"
#include "cmsis_os.h"

/* 震动任务句柄（用于连续震动模式） */
static osThreadId_t MotorTaskHandle = NULL;
static volatile uint8_t motor_continuous = 0;

/* ────────────────────────────────────────────
 * GPIO 初始化
 * ──────────────────────────────────────────── */
void Motor_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* 默认关闭 */
    HAL_GPIO_WritePin(MOTOR_PORT, MOTOR_PIN, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin   = MOTOR_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(MOTOR_PORT, &GPIO_InitStruct);
}

/* ────────────────────────────────────────────
 * 震动指定模式（非阻塞，用 osDelay）
 * ──────────────────────────────────────────── */
void Motor_Vibrate(MotorPattern_t pattern)
{
    switch (pattern)
    {
    case MOTOR_SHORT:
        MOTOR_ON();
        osDelay(100);
        MOTOR_OFF();
        break;

    case MOTOR_LONG:
        MOTOR_ON();
        osDelay(500);
        MOTOR_OFF();
        break;

    case MOTOR_DOUBLE:
        MOTOR_ON();
        osDelay(100);
        MOTOR_OFF();
        osDelay(100);
        MOTOR_ON();
        osDelay(100);
        MOTOR_OFF();
        break;

    default:
        break;
    }
}

/* ────────────────────────────────────────────
 * 连续震动任务
 * ──────────────────────────────────────────── */
static void Motor_ContinuousTask(void *argument)
{
    (void)argument;
    while (motor_continuous)
    {
        MOTOR_ON();
        osDelay(200);
        MOTOR_OFF();
        osDelay(200);
    }
    MotorTaskHandle = NULL;
    osThreadExit();
}

/* ────────────────────────────────────────────
 * 开始连续震动
 * ──────────────────────────────────────────── */
void Motor_ContinuousStart(void)
{
    if (motor_continuous) return;  /* 已经在震 */

    motor_continuous = 1;

    const osThreadAttr_t attr = {
        .name       = "MotorTask",
        .stack_size = 128,
        .priority   = osPriorityLow,
    };
    MotorTaskHandle = osThreadNew(Motor_ContinuousTask, NULL, &attr);
}

/* ────────────────────────────────────────────
 * 停止连续震动
 * ──────────────────────────────────────────── */
void Motor_ContinuousStop(void)
{
    motor_continuous = 0;
    MOTOR_OFF();
}
