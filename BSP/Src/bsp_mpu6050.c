/**
 * @file    bsp_mpu6050.c
 * @brief   MPU6050 加速度读取实现。
 */
#include "bsp.h"
#include "bsp_mpu6050.h"
#include "app_rtos.h"

#define MPU_TIMEOUT_MS   50u

static int reg_write(uint8_t reg, uint8_t val)
{
  uint16_t addr = (uint16_t)(MPU6050_ADDR << 1);
  uint8_t  buf[2];
  HAL_StatusTypeDef st;
  buf[0] = reg;
  buf[1] = val;
  if (mtxI2c != NULL) osMutexAcquire(mtxI2c, osWaitForever);
  st = HAL_I2C_Master_Transmit(&hi2c2, addr, buf, 2u, MPU_TIMEOUT_MS);
  if (mtxI2c != NULL) osMutexRelease(mtxI2c);
  return (st == HAL_OK) ? 0 : -1;
}

static int reg_read(uint8_t reg, uint8_t *val)
{
  uint16_t addr = (uint16_t)(MPU6050_ADDR << 1);
  HAL_StatusTypeDef st;

  if (mtxI2c != NULL) osMutexAcquire(mtxI2c, osWaitForever);
  st = HAL_I2C_Master_Transmit(&hi2c2, addr, &reg, 1u, MPU_TIMEOUT_MS);
  if (st == HAL_OK)
  {
    st = HAL_I2C_Master_Receive(&hi2c2, (uint16_t)(addr | 0x01u), val, 1u, MPU_TIMEOUT_MS);
  }
  if (mtxI2c != NULL) osMutexRelease(mtxI2c);
  return (st == HAL_OK) ? 0 : -1;
}

int BspMpu6050_Init(void)
{
  uint8_t who = 0u;

  if (reg_read(MPU6050_WHO_AM_I, &who) != 0)
  {
    return -1;
  }
  if ((who & 0x7Eu) != (MPU6050_ADDR & 0x7Eu))
  {
    return -1;
  }
  /* 唤醒并解除复位 */
  reg_write(MPU6050_PWR_MGMT, 0x00u);
  reg_write(MPU6050_SMPLRT_DIV, 0x07u);   /* 1kHz/8 = 125Hz 采样 */
  reg_write(MPU6050_CONFIG, 0x00u);       /* DLPF 关闭 */
  reg_write(MPU6050_ACCEL_CFG, 0x00u);    /* ±2g */
  return 0;
}

int BspMpu6050_ReadAccel(int16_t *ax, int16_t *ay, int16_t *az)
{
  uint8_t  buf[6];
  uint16_t addr = (uint16_t)(MPU6050_ADDR << 1);
  uint8_t  reg  = MPU6050_ACCEL_X_H;
  HAL_StatusTypeDef st;

  if ((ax == NULL) || (ay == NULL) || (az == NULL)) return -1;

  if (mtxI2c != NULL) osMutexAcquire(mtxI2c, osWaitForever);
  st = HAL_I2C_Master_Transmit(&hi2c2, addr, &reg, 1u, MPU_TIMEOUT_MS);
  if (st == HAL_OK)
  {
    st = HAL_I2C_Master_Receive(&hi2c2, (uint16_t)(addr | 0x01u), buf, 6u, MPU_TIMEOUT_MS);
  }
  if (mtxI2c != NULL) osMutexRelease(mtxI2c);
  if (st != HAL_OK)
  {
    return -1;
  }
  *ax = (int16_t)(((uint16_t)buf[0] << 8u) | buf[1]);
  *ay = (int16_t)(((uint16_t)buf[2] << 8u) | buf[3]);
  *az = (int16_t)(((uint16_t)buf[4] << 8u) | buf[5]);
  return 0;
}