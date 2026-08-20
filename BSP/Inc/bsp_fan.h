/**
 * @file    bsp_fan.h
 * @brief   风扇（TB6612, TIM4_CH1 PA11, PWM 调速）。
 *          方向/使能固定接线，仅单调速信号。
 */
#ifndef BSP_FAN_H
#define BSP_FAN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** 初始化（关闭 PWM 输出） */
void BspFan_Init(void);

/** 设置转速百分比 0~100（0=停） */
void BspFan_SetSpeed(uint8_t percent);

#ifdef __cplusplus
}
#endif

#endif /* BSP_FAN_H */