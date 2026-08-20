/**
 * @file    bsp_bh1750.c
 * @brief   BH1750 光照传感器实现（I2C2）。
 *          地址自动探测: ADDR 接地=0x23, ADDR 拉高=0x5C。
 *          所有 I2C 访问受 mtxI2c 保护(与 OLED/MPU6050 共用总线)。
 */
#include "bsp.h"
#include "bsp_bh1750.h"
#include "app_rtos.h"

#define BH1750_TIMEOUT_MS  100u

/* 诊断统计（Watch 可查） */
volatile uint8_t  g_bh_ok       = 0u;
volatile uint16_t g_bh_ok_cnt   = 0u;
volatile uint16_t g_bh_fail_cnt = 0u;
volatile uint16_t g_bh_raw      = 0u;   /* 最近一次原始 16 位读数 */

static uint8_t s_addr = BH1750_ADDR;   /* 探测到的地址 */

int BspBh1750_Init(void)
{
  static const uint8_t cand[2] = { 0x23u, 0x5Cu };   /* 常见两种 ADDR 接法 */
  uint8_t  i, cmd;
  HAL_StatusTypeDef st;

  for (i = 0u; i < 2u; i++)
  {
    s_addr = cand[i];

    cmd = BH1750_CMD_POWERON;
    if (mtxI2c != NULL) osMutexAcquire(mtxI2c, osWaitForever);
    st = HAL_I2C_Master_Transmit(&hi2c2, (uint16_t)(s_addr << 1), &cmd, 1u, BH1750_TIMEOUT_MS);
    if (mtxI2c != NULL) osMutexRelease(mtxI2c);
    if (st != HAL_OK) continue;

    BSP_DelayMs(120u);          /* BH1750 上电测量周期 */
    cmd = BH1750_CMD_CONT_H;
    if (mtxI2c != NULL) osMutexAcquire(mtxI2c, osWaitForever);
    st = HAL_I2C_Master_Transmit(&hi2c2, (uint16_t)(s_addr << 1), &cmd, 1u, BH1750_TIMEOUT_MS);
    if (mtxI2c != NULL) osMutexRelease(mtxI2c);
    if (st != HAL_OK) continue;

    return 0;                   /* 探测成功 */
  }
  return -1;
}

int BspBh1750_ReadLux(uint16_t *lux)
{
  uint8_t  buf[2];
  uint16_t raw;
  HAL_StatusTypeDef st;

  if (lux == NULL) return -1;

  if (mtxI2c != NULL) osMutexAcquire(mtxI2c, osWaitForever);
  st = HAL_I2C_Master_Receive(&hi2c2, (uint16_t)((s_addr << 1) | 0x01u), buf, 2u, BH1750_TIMEOUT_MS);
  if (mtxI2c != NULL) osMutexRelease(mtxI2c);
  if (st != HAL_OK)
  {
    g_bh_ok = 0u;
    g_bh_fail_cnt++;
    return -1;
  }

  raw = (uint16_t)(((uint16_t)buf[0] << 8u) | buf[1]);
  *lux = (uint16_t)(raw / 1.2f);

  g_bh_raw = raw;
  g_bh_ok  = 1u;
  g_bh_ok_cnt++;
  return 0;
}