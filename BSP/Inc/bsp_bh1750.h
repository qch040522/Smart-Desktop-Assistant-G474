/**
 * @file    bsp_bh1750.h
 * @brief   BH1750 光照传感器（I2C2, ADDR=0x23）。
 */
#ifndef BSP_BH1750_H
#define BSP_BH1750_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define BH1750_ADDR        (0x23u)

#define BH1750_CMD_POWERON 0x01u
#define BH1750_CMD_CONT_H  0x10u  /* 连续高分辨率 1lx */

/** 初始化（上电 + 连续高分辨率模式） */
int BspBh1750_Init(void);

/** 读取光照（1lx 单位） */
int BspBh1750_ReadLux(uint16_t *lux);

/* ============ 调试用（可在调试器 Watch 窗口查看） ============ */
extern volatile uint8_t  g_bh_ok;       /* 最近一次读取是否成功 */
extern volatile uint16_t g_bh_ok_cnt;   /* 累计成功次数 */
extern volatile uint16_t g_bh_fail_cnt; /* 累计失败次数 */
extern volatile uint16_t g_bh_raw;      /* 最近一次原始 16 位读数 */

#ifdef __cplusplus
}
#endif

#endif /* BSP_BH1750_H */