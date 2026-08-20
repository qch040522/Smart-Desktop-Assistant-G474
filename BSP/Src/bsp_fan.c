/**
 * @file    bsp_fan.c
 * @brief   风扇 PWM 调速实现（TIM4_CH1, PA11）。
 */
#include "bsp.h"
#include "bsp_fan.h"

#define FAN_PERIOD  8500u   /* 与 CubeMX 一致 */

void BspFan_Init(void)
{
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_1);
  BspFan_SetSpeed(0u);
}

void BspFan_SetSpeed(uint8_t percent)
{
  uint32_t p = (percent > 100u) ? 100u : percent;
  uint32_t ccr = (FAN_PERIOD * p) / 100u;
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, (uint32_t)ccr);
}