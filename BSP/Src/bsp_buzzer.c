/**
 * @file    bsp_buzzer.c
 * @brief   无源蜂鸣器变频驱动。
 *   TIM3 时钟 = APB1 Timer 170MHz；PSC=1 -> 85MHz 计数。
 *   ARR = 85MHz/freq, 占空比 50%。
 */
#include "bsp.h"
#include "bsp_buzzer.h"

/* TIM3 时钟 = APB1 Timer 170MHz；PSC=1 -> 85MHz 计数 */
#define BUZZER_CLK_HZ    (85000000u)

void BspBuzzer_Init(void)
{
  __HAL_TIM_SET_PRESCALER(&htim3, 1u);
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
  BspBuzzer_BeepOff();
}

void BspBuzzer_BeepOn(uint16_t freq_hz)
{
  uint32_t arr;

  if ((freq_hz < 1000u) || (freq_hz > 8000u))
  {
    freq_hz = 3000u;
  }
  arr = BUZZER_CLK_HZ / (uint32_t)freq_hz;
  __HAL_TIM_SET_AUTORELOAD(&htim3, arr);
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, arr / 2u);
  /* 强制产生更新事件使 ARR 生效（预装载） */
  htim3.Instance->EGR = TIM_EGR_UG;
}

void BspBuzzer_BeepOff(void)
{
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0u);
}