#include "em70x8.h"

#define CLK_ENABLE __HAL_RCC_GPIOB_CLK_ENABLE();
iic_bus_t EM7028_bus = 
{
	.IIC_SDA_PORT = GPIOB,
	.IIC_SCL_PORT = GPIOB,
	.IIC_SDA_PIN  = GPIO_PIN_13,
	.IIC_SCL_PIN  = GPIO_PIN_14,
};

uint8_t  EM7028_ReadOneReg(unsigned char RegAddr)
{
	unsigned char dat;
	dat = IIC_Read_One_Byte(&EM7028_bus, EM7028_ADDR, RegAddr);
	return dat;
}

void  EM7028_WriteOneReg(unsigned char RegAddr, unsigned char dat)
{
	IIC_Write_One_Byte(&EM7028_bus, EM7028_ADDR, RegAddr, dat);
}

uint8_t EM7028_Get_ID()
{
	return EM7028_ReadOneReg(ID_REG);
}

uint8_t EM7028_hrs_init()
{
	uint8_t i = 5;
	
	CLK_ENABLE;
	IICInit(&EM7028_bus);
	
	while(EM7028_Get_ID() != 0x36 && i)
	{
		HAL_Delay(100);
		i--;
	}
	if(!i)
	{return 1;}
	EM7028_WriteOneReg(HRS_CFG,0x00);				
	//HRS1_EN, HRS2_dis
	//Heart Beat Measurement is enabled with LED1 turned on and only Red Light Sensor and IR sensor enabled. 
	//When LED1 turned on, the result stores to HRS_DATA0;
	EM7028_WriteOneReg(HRS2_DATA_OFFSET, 0x00);
	//0 offset
	EM7028_WriteOneReg(HRS2_GAIN_CTRL, 0x7f);		
	//HRS2 GAIN = 1
	EM7028_WriteOneReg(HRS1_CTRL, 0x47);
	//HRS1 GAIN =1, HRS1 RANGE =8, HRS1 FREQ = 2.62144MHz (1.5625ms), HRS1 RES = 16 bits, HRS1 mode
	EM7028_WriteOneReg(INT_CTRL, 0x00);
	//LED programmed current = 2.5mA
	return 0;
}

uint8_t EM7028_hrs_Enable()
{
	uint8_t i = 5;
	while(EM7028_Get_ID() != 0x36 && i)
	{
		HAL_Delay(100);
		i--;
	}
	if(!i)
	{return 1;}
	EM7028_WriteOneReg(HRS_CFG,0x08);
	return 0;
}

uint8_t EM7028_hrs_DisEnable()
{
	uint8_t i = 5;
	while(EM7028_Get_ID() != 0x36 && i)
	{
		HAL_Delay(100);
		i--;
	}
	if(!i)
	{return 1;}
	EM7028_WriteOneReg(HRS_CFG,0x00);
	return 0;
}

uint16_t EM7028_Get_HRS1(void)
{
	uint16_t dat;
	dat = EM7028_ReadOneReg(HRS1_DATA0_H);
	dat <<= 8;
	dat |= EM7028_ReadOneReg(HRS1_DATA0_L);
	return dat;
}

uint16_t EM7028_Get_HRS2(void)
{
	uint16_t dat;
	dat = EM7028_ReadOneReg(HRS2_DATA0_H);
	dat <<= 8;
	dat |= EM7028_ReadOneReg(HRS2_DATA0_L);
	return dat;
}

/* ======================= SpO2 Reference Estimation =======================
 * NOTE: EM7028 has only 525nm green LED, no Red (660nm) or IR (940nm).
 * Real SpO2 requires dual-wavelength absorption ratio (Beer-Lambert law).
 * This module provides ONLY a rough reference estimate based on HR and
 * PPG perfusion index. NOT medically accurate.
 */

#define SPO2_BUF_LEN     100   /* 5 seconds at 20Hz */
#define SPO2_MIN_PPG     1000  /* Minimum valid PPG amplitude */

typedef struct {
    uint16_t buf[SPO2_BUF_LEN];
    uint8_t idx;
    uint8_t count;
    uint16_t ppg_min;
    uint16_t ppg_max;
    uint32_t ppg_sum;
    uint8_t ready;
} SPO2_State_t;

static SPO2_State_t g_spo2;

void SPO2_AlgoInit(void)
{
    memset(&g_spo2, 0, sizeof(g_spo2));
    g_spo2.ppg_min = 0xFFFF;
}

uint8_t SPO2_Estimate(uint16_t ppg_val, uint8_t hr)
{
    /* Feed into circular buffer */
    g_spo2.buf[g_spo2.idx] = ppg_val;
    g_spo2.idx = (g_spo2.idx + 1) % SPO2_BUF_LEN;
    if (g_spo2.count < SPO2_BUF_LEN) g_spo2.count++;

    if (g_spo2.count < SPO2_BUF_LEN) {
        return 0; /* Not enough data yet */
    }

    /* Find min/max in current buffer */
    uint16_t min_v = 0xFFFF;
    uint16_t max_v = 0;
    uint32_t sum = 0;
    for (uint8_t i = 0; i < SPO2_BUF_LEN; i++) {
        uint16_t v = g_spo2.buf[i];
        if (v < min_v) min_v = v;
        if (v > max_v) max_v = v;
        sum += v;
    }
    g_spo2.ppg_min = min_v;
    g_spo2.ppg_max = max_v;
    g_spo2.ppg_sum = sum;
    g_spo2.ready = 1;

    /* Validate signal */
    if (max_v < SPO2_MIN_PPG || (max_v - min_v) < 100) {
        return 0; /* Weak or flat signal */
    }

    /* DC level and AC amplitude */
    uint16_t dc = (uint16_t)(sum / SPO2_BUF_LEN);
    uint16_t ac = max_v - min_v;

    /* Perfusion Index (%) = AC / DC * 100 */
    uint8_t pi = (uint8_t)(((uint32_t)ac * 100) / dc);

    /* ====== Reference Estimation (NOT real SpO2) ====== */
    int16_t spo2 = 98;

    if (hr > 0) {
        if (hr > 100) {
            spo2 -= (hr - 100) / 8;
        }
        if (hr < 50) {
            spo2 -= (50 - hr) / 10;
        }
    }

    if (pi < 3) {
        spo2 -= 4;
    } else if (pi < 5) {
        spo2 -= 2;
    }

    if (spo2 > 100) spo2 = 100;
    if (spo2 < 85) spo2 = 85;

    return (uint8_t)spo2;
}

uint8_t SPO2_GetPerfusionIndex(void)
{
    if (!g_spo2.ready) return 0;
    uint16_t dc = (uint16_t)(g_spo2.ppg_sum / SPO2_BUF_LEN);
    uint16_t ac = g_spo2.ppg_max - g_spo2.ppg_min;
    if (dc == 0) return 0;
    return (uint8_t)(((uint32_t)ac * 100) / dc);
}
