/**
 ****************************************************************************************************
 * @file        GT9XXX.h
 * @author      正点原子团队(正点原子)
 * @version     V1.1
 * @date        2023-12-1
 * @brief       4.3寸电容触摸屏-GT9xxx 驱动代码
 *   @note      GT系列电容触摸屏IC通用驱动,本代码支持: GT9147/GT917S/GT968/GT1151/GT9271 等多种
 *              驱动IC, 这些驱动IC仅ID不一样, 具体代码基本不需要做任何修改即可通过本代码直接驱动
 *
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:正点原子 ESP32S3 开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 *
 ****************************************************************************************************
 */

#ifndef __GT9XXX_H
#define __GT9XXX_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "touch.h"
#include "string.h"
#include "iic.h"
#include "xl9555.h"


/******************************************************************************************/
/* GT9XXX INT引脚 定义 */
#define GT9XXX_INT_GPIO_PIN             GPIO_NUM_40
#define GT9XXX_IIC_PORT                 I2C_NUM_1
#define GT9XXX_IIC_SDA                  GPIO_NUM_39
#define GT9XXX_IIC_CLK                  GPIO_NUM_38
#define GT9XXX_IIC_FREQ                 400000                                  /* IIC FREQ */
#define GT9XXX_INT                      gpio_get_level(GT9XXX_INT_GPIO_PIN)     /* 中断引脚 */

/* RGB_BL */
#define CT_RST(x)       do { x ?                              \
                            xl9555_pin_write(CT_RST_IO, 1):   \
                            xl9555_pin_write(CT_RST_IO, 0);   \
                        } while(0)

/* IIC读写命令 */
#define GT9XXX_CMD_WR                   0X28        /* 写命令 */
#define GT9XXX_CMD_RD                   0X29        /* 读命令 */

/* GT9XXX 部分寄存器定义  */
#define GT9XXX_CTRL_REG                 0X8040      /* GT9XXX控制寄存器 */
#define GT9XXX_CFGS_REG                 0X8047      /* GT9XXX配置起始地址寄存器 */
#define GT9XXX_CHECK_REG                0X80FF      /* GT9XXX校验和寄存器 */
#define GT9XXX_PID_REG                  0X8140      /* GT9XXX产品ID寄存器 */

#define GT9XXX_GSTID_REG                0X814E      /* GT9XXX当前检测到的触摸情况 */
#define GT9XXX_TP1_REG                  0X8150      /* 第一个触摸点数据地址 */
#define GT9XXX_TP2_REG                  0X8158      /* 第二个触摸点数据地址 */
#define GT9XXX_TP3_REG                  0X8160      /* 第三个触摸点数据地址 */
#define GT9XXX_TP4_REG                  0X8168      /* 第四个触摸点数据地址 */
#define GT9XXX_TP5_REG                  0X8170      /* 第五个触摸点数据地址 */
#define GT9XXX_TP6_REG                  0X8178      /* 第六个触摸点数据地址 */
#define GT9XXX_TP7_REG                  0X8180      /* 第七个触摸点数据地址 */
#define GT9XXX_TP8_REG                  0X8188      /* 第八个触摸点数据地址 */
#define GT9XXX_TP9_REG                  0X8190      /* 第九个触摸点数据地址 */
#define GT9XXX_TP10_REG                 0X8198      /* 第十个触摸点数据地址 */

/* 函数声明 */
uint8_t gt9xxx_init(void);                                          /* 初始化gt9xxx触摸屏 */
uint8_t gt9xxx_scan(uint8_t mode);                                  /* 扫描触摸屏(采用查询方式) */
esp_err_t gt9xxx_wr_reg(uint16_t reg, uint8_t *buf, uint8_t len);   /* 向gt9xxx写入数据 */
esp_err_t gt9xxx_rd_reg(uint16_t reg, uint8_t *buf, uint8_t len);   /* 从gt9xxx读出数据 */

#endif
