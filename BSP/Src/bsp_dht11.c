/**
 * @file    bsp_dht11.c
 * @brief   DHT11 单总线时序驱动（PB15, 4.7k 上拉）。
 *          注意: 关中断段仅发生在本函数内部，避免与任务切换冲突。
 */
#include "bsp.h"
#include "bsp_dht11.h"
#include "main.h"

#define DHT11_PIN   GPIO_PIN_15
#define DHT11_PORT  GPIOB

/* ============ 调试统计（供 Watch 查看, 定位读不到/数值错误） ============ */
volatile uint8_t  g_dht_raw[5]   = {0};
volatile uint8_t  g_dht_ok       = 0u;
volatile uint16_t g_dht_ok_cnt   = 0u;
volatile uint16_t g_dht_fail_cnt = 0u;

static void set_output(void)
{
  GPIO_InitTypeDef g = {0};
  g.Pin   = DHT11_PIN;
  g.Mode  = GPIO_MODE_OUTPUT_PP;   /* 驱动低电平更强 */
  g.Pull  = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(DHT11_PORT, &g);
}

static void set_input(void)
{
  GPIO_InitTypeDef g = {0};
  g.Pin  = DHT11_PIN;
  g.Mode = GPIO_MODE_INPUT;
  g.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(DHT11_PORT, &g);
}

void BspDht11_Init(void)
{
  set_output();
  HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_SET); /* 空闲拉高 */
  BSP_DelayUs(1u);   /* 提前初始化 DWT 微秒计时器 */
}

/* 采样一位: 返回 0/1, 失败返回 -1。
 * 用 DWT 精确微秒计时, 必须在关中断下调用(避免 FreeRTOS/串口中断破坏时序)。
 * DHT11: "0"码高电平 26~28us, "1"码高电平 ~70us, 以 40us 为界(兼容克隆件)。 */
static int read_bit(void)
{
  uint32_t cyc_per_us = (BSP_CORE_CLK_HZ / 1000000u);
  uint32_t t0;

  /* 等引脚拉高(该位低电平 ~50us 结束), 超时 100us 判失败 */
  t0 = DWT->CYCCNT;
  while (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_RESET)
  {
    if ((DWT->CYCCNT - t0) > (uint32_t)100u * cyc_per_us) return -1;
  }

  /* 测量高电平持续时间 */
  t0 = DWT->CYCCNT;
  while (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_SET)
  {
    if ((DWT->CYCCNT - t0) > (uint32_t)100u * cyc_per_us) return -1;
  }

  return ((DWT->CYCCNT - t0) > (uint32_t)40u * cyc_per_us) ? 1 : 0;
}

int BspDht11_Read(dht11_data_t *out)
{
  uint8_t data[5] = {0};
  int     i, r;
  uint8_t check;
  uint32_t primask;
  uint32_t cyc_per_us = (BSP_CORE_CLK_HZ / 1000000u);
  uint32_t t0;

  if (out == NULL) return -1;

  /* 1. 起始: 拉低 >=18ms（此段非时序关键, 保持中断开启） */
  set_output();
  HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_RESET);
  HAL_Delay(20u);
  HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_SET);
  /* 主机释放后等待 20-40us */
  BSP_DelayUs(30u);

  /* 2. 应答 + 40bit 属时序关键段, 关中断采样(约 3~4ms) */
  primask = __get_PRIMASK();
  __disable_irq();

  set_input();

  /* 应答段必须完整消费(低 80us + 高 80us), 否则 40bit 错位。
   * 这里不能用短循环计数(关中断后循环变快会提前退出), 一律用 DWT 超时。 */
  t0 = DWT->CYCCNT;                                   /* 等应答开始(线拉低) */
  while (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_SET)
  { if ((DWT->CYCCNT - t0) > (uint32_t)200u * cyc_per_us) break; }

  t0 = DWT->CYCCNT;                                   /* 应答低 ~80us: 等线拉高 */
  while (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_RESET)
  { if ((DWT->CYCCNT - t0) > (uint32_t)200u * cyc_per_us) break; }

  t0 = DWT->CYCCNT;                                   /* 应答高 ~80us: 等线拉低 */
  while (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_SET)
  { if ((DWT->CYCCNT - t0) > (uint32_t)200u * cyc_per_us) break; }

  /* 3. 读取 40 bit (5 字节) */
  for (i = 0; i < 40; i++)
  {
    r = read_bit();
    if (r < 0)
    {
      __set_PRIMASK(primask);          /* 失败也要恢复中断 */
      g_dht_ok = 0u;
      g_dht_fail_cnt++;
      out->ok = 0;
      return -1;
    }
    data[i / 8] = (uint8_t)((data[i / 8] << 1) | (uint8_t)r);
  }

  __set_PRIMASK(primask);              /* 恢复中断 */

  check = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
  if (check != data[4])
  {
    g_dht_ok = 0u;
    g_dht_fail_cnt++;
    out->ok = 0;
    return -1;
  }

  g_dht_raw[0] = data[0];
  g_dht_raw[1] = data[1];
  g_dht_raw[2] = data[2];
  g_dht_raw[3] = data[3];
  g_dht_raw[4] = data[4];
  g_dht_ok       = 1u;
  g_dht_ok_cnt++;

  out->humi_pct = data[0];
  out->temp_x10 = (int16_t)(data[2] * 10 + data[3]);   /* 含小数位(0.1°C) */
  out->ok       = 1;
  return 0;
}