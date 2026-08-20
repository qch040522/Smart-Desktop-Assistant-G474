/**
 * @file    bsp_ws2812.h
 * @brief   WS2812 台灯（PA9/TIM1_CH2 + DMA, 800kHz NRZ）。
 *          仅蓝色: G=0/R=0/B=亮度(0~255)（需求 §五/§引脚规划）。
 */
#ifndef BSP_WS2812_H
#define BSP_WS2812_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** 灯带长度（灯珠数，按需修改；本工程默认 1） */
#define WS2812_LED_NUM      1u

/** 初始化: 确保 TIM1 PWM+DMA 链路就绪 */
void BspWs2812_Init(void);

/**
 * 设置台灯蓝色亮度 (0~255)，同步发送（内部等待上一次发送完成）。
 */
void BspWs2812_SetBlue(uint8_t brightness);

/** 关闭（发送全 0 即灭） */
void BspWs2812_Off(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_WS2812_H */