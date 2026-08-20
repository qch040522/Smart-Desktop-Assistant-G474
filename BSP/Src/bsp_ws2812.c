/**
 * @file    bsp_ws2812.c
 * @brief   WS2812 时序实现。
 *
 *   TIM1(170MHz) ARR=212 -> 输出周期 1.247us (~802kHz)
 *   每 bit 为一个 PWM 周期, CCR 决定高电平长度:
 *     0 码: 高 0.40us  -> CCR ~= 68
 *     1 码: 高 0.85us  -> CCR ~= 144
 *   DMA 内存->CCR 半字, 24 bit/灯。传输完成回调把 CCR 拉零(复位码)。
 */
#include "bsp.h"
#include "bsp_ws2812.h"

/* 高电平 ticks 对应 bit */
#define WS_BIT0_CCR      68u
#define WS_BIT1_CCR      144u
#define WS_BYTE_BITS     8u

static volatile uint8_t s_ws_tx_busy = 0u;
static uint16_t s_buf[WS2812_LED_NUM * 24u];

void BspWs2812_Init(void)
{
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 0u);
  s_ws_tx_busy = 0u;
}

/**
 * PWM-DMA 传输完成回调（HAL_TIM_PWM_Start_DMA -> TIM_DMADelayPulseCplt 触发）。
 *
 * 注意: 本回调名必须是 HAL_TIM_PWM_PulseFinishedCallback（HAL 中不存在
 * "HAL_TIM_PWM_DMAStopCpltCallback"）。传输完毕即把 CCR 拉零进入复位码
 * （>=50us 低电平），并清零忙碌标志，供下一次调光继续。
 */
void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM1)
  {
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 0u);
    s_ws_tx_busy = 0u;
  }
}

static void fill_frame(uint8_t g, uint8_t r, uint8_t b)
{
  uint32_t i, led, bit;
  uint32_t idx = 0u;
  const  uint8_t ch[3] = { g, r, b };   /* WS2812 顺序为 GRB */

  for (led = 0u; led < WS2812_LED_NUM; led++)
  {
    for (i = 0u; i < 3u; i++)
    {
      for (bit = 0u; bit < 8u; bit++)
      {
        uint8_t v = (uint8_t)((ch[i] >> (7u - bit)) & 0x01u);
        s_buf[idx++] = (v ? WS_BIT1_CCR : WS_BIT0_CCR);
      }
    }
  }
}

void BspWs2812_SetBlue(uint8_t brightness)
{
  uint32_t t0 = HAL_GetTick();

  /* 等待上一次 DMA 传输完成（带超时兜底，避免异常时死等） */
  while (s_ws_tx_busy != 0u)
  {
    if ((HAL_GetTick() - t0) > 100u)
    {
      return;   /* 上次发送仍未完成(异常), 放弃本次更新, 等待下次重试 */
    }
  }

  fill_frame(0u, 0u, brightness);

  s_ws_tx_busy = 1u;
  if (HAL_TIM_PWM_Start_DMA(&htim1, TIM_CHANNEL_2, (uint32_t *)s_buf,
                            (uint16_t)(WS2812_LED_NUM * 24u)) != HAL_OK)
  {
    s_ws_tx_busy = 0u;
  }
}

void BspWs2812_Off(void)
{
  BspWs2812_SetBlue(0u);
}