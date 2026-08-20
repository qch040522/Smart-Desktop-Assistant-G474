/**
 * @file    bsp.h
 * @brief   板级支持层公共声明。
 *
 *  本层封装 HAL 之上、业务之下，仅处理"外设怎么操作"。
 *  外设句柄来自 CubeMX 生成的 main.c（extern 引用）。
 */
#ifndef BSP_H
#define BSP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"          /* CubeMX 包含 stm32g4xx_hal.h 等 */

/* ==================== CubeMX 外设句柄 ==================== */
extern I2C_HandleTypeDef hi2c2;      /* OLED/BH1750/MPU6050 */
extern UART_HandleTypeDef huart3;    /* ESP32 通信 */
extern UART_HandleTypeDef huart5;    /* 淘晶驰串口屏 */
extern TIM_HandleTypeDef htim1;      /* WS2812 (TIM1_CH2 + DMA) */
extern TIM_HandleTypeDef htim3;      /* 蜂鸣器 (TIM3_CH1) */
extern TIM_HandleTypeDef htim4;      /* 风扇 (TIM4_CH1) */
extern DMA_HandleTypeDef hdma_tim1_ch2;
extern RTC_HandleTypeDef hrtc;
extern IWDG_HandleTypeDef hiwdg;

/* ==================== 公共工具 ==================== */
#define BSP_CORE_CLK_HZ     170000000uL  /* G474 @170MHz */
uint32_t BSP_GetTick(void);            /* 系统毫秒 (HAL_GetTick) */
void     BSP_DelayMs(uint32_t ms);     /* 阻塞延时(业务内慎用) */
void     BSP_DelayUs(uint32_t us);     /* DWT 微秒延时(关中断时可用) */
void     BSP_IwdgFeed(void);           /* 喂硬件看门狗 */

extern volatile uint8_t g_u3_rx_byte;   /* ESP32 接收缓存 */
extern volatile uint8_t g_u5_rx_byte;   /* TJC 接收缓存 */

/* ==================== I2C 总线扫描（调试用） ==================== */
extern volatile uint8_t g_i2c_scan[128];    /* 各 7 位地址是否有设备响应 */
extern volatile uint8_t g_i2c_scan_count;   /* 响应的设备数 */
void BspI2c_ScanBus(void);                  /* 扫描 1~127 地址(启动时调用) */

#ifdef __cplusplus
}
#endif

#endif /* BSP_H */