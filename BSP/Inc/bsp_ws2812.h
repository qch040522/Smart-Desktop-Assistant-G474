/**
 * @file    bsp_ws2812.h
 * @brief   WS2812 白光灯带（PA9/TIM1_CH2 + DMA, 800kHz NRZ）。
 *          白光: G=R=B=亮度(0~255)。
 */
#ifndef BSP_WS2812_H
#define BSP_WS2812_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** 灯带长度（灯珠数，按实际灯带修改；默认 8） */
#define WS2812_LED_NUM      10u

/** 初始化: 确保 TIM1 PWM+DMA 链路就绪 */
void BspWs2812_Init(void);

/**
 * 设置灯带白光亮度 (0~255)，同步发送（内部等待上一次发送完成）。
 */
void BspWs2812_SetBright(uint8_t brightness);

/** 关闭（发送全 0 即灭） */
void BspWs2812_Off(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_WS2812_H */