/**
 * @file    bsp_mpu6050.h
 * @brief   MPU6050 加速度计（I2C2, AD0=0, 地址 0x68）。
 *          用于"基准角失效检测"三轴挪动嗅探（方案A，需求 §5）。
 */
#ifndef BSP_MPU6050_H
#define BSP_MPU6050_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define MPU6050_ADDR       (0x68u)
#define MPU6050_WHO_AM_I   0x75u
#define MPU6050_PWR_MGMT   0x6Bu
#define MPU6050_ACCEL_X_H  0x3Bu
#define MPU6050_SMPLRT_DIV 0x19u
#define MPU6050_CONFIG     0x1Au
#define MPU6050_GYRO_CFG   0x1Bu
#define MPU6050_ACCEL_CFG  0x1Cu
#define MPU6050_INT_EN     0x38u

/** 初始化（唤醒 + ±2g + 低力矩） */
int BspMpu6050_Init(void);

/** 读取三轴加速度原始值 */
int BspMpu6050_ReadAccel(int16_t *ax, int16_t *ay, int16_t *az);

#ifdef __cplusplus
}
#endif

#endif /* BSP_MPU6050_H */