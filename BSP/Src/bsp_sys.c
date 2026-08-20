/**
 * @file    bsp_sys.c
 * @brief   板级公共工具实现：延时 / 看门狗 / I2C 总线扫描。
 */
#include "bsp.h"
#include "app_rtos.h"

/* ============ I2C 总线扫描（调试: 判断设备是否在总线上/地址对否） ============ */
volatile uint8_t g_i2c_scan[128];
volatile uint8_t g_i2c_scan_count;

void BspI2c_ScanBus(void)
{
  uint8_t i;

  g_i2c_scan_count = 0u;
  for (i = 1u; i < 128u; i++)
  {
    g_i2c_scan[i] = 0u;
    if (mtxI2c != NULL) osMutexAcquire(mtxI2c, osWaitForever);
    HAL_StatusTypeDef st = HAL_I2C_IsDeviceReady(&hi2c2, (uint16_t)(i << 1), 1u, 2u);
    if (mtxI2c != NULL) osMutexRelease(mtxI2c);
    if (st == HAL_OK)
    {
      g_i2c_scan[i] = 1u;
      g_i2c_scan_count++;
    }
  }
}

/* ---------------------------------------------------------------- */
uint32_t BSP_GetTick(void)
{
  return HAL_GetTick();
}

/* ---------------------------------------------------------------- */
void BSP_DelayMs(uint32_t ms)
{
  HAL_Delay(ms);
}

/* ---------------------------------------------------------------- */
/* DWT 微秒延时（不依赖 SysTick；可在关中断场景使用） */
static uint8_t s_dwt_ready = 0u;

static void dwt_init(void)
{
  if (!(CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk))
  {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  }
  DWT->CYCCNT = 0u;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  s_dwt_ready = 1u;
}

void BSP_DelayUs(uint32_t us)
{
  uint32_t start, cnt;

  if (!s_dwt_ready)
  {
    dwt_init();
  }
  /* CPU 170MHz, 每 us 约 170 计数 */
  start = DWT->CYCCNT;
  cnt   = us * (BSP_CORE_CLK_HZ / 1000000u);
  while ((DWT->CYCCNT - start) < cnt)
  {
  }
}

/* ---------------------------------------------------------------- */
void BSP_IwdgFeed(void)
{
  /* HAL_IWDG_Refresh 复位窗口计数器 */
  if (HAL_IWDG_Refresh(&hiwdg) != HAL_OK)
  {
    /* 忽略; IWDG 配置后不可改变 */
  }
}