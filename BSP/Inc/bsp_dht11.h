/**
 * @file    bsp_dht11.h
 * @brief   DHT11 温湿度传感器（单总线 / PB15，需求 §引脚规划）。
 */
#ifndef BSP_DHT11_H
#define BSP_DHT11_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct {
  int16_t  temp_x10;   /* 0.1°C 有符号 */
  uint8_t  humi_pct;
  uint8_t  ok;         /* 本次读取是否有效 */
} dht11_data_t;

/** 初始化 PB15（已在 CubeMX 配为开漏输出，本函数做空闲上拉） */
void BspDht11_Init(void);

/**
 * 阻塞读取一次（约 20ms，调用前请确保任务上下文允许阻塞）。
 * 返回 0=成功, 非0=失败。
 */
int BspDht11_Read(dht11_data_t *out);

/* ============ 调试用（可在调试器 Watch 窗口查看） ============ */
extern volatile uint8_t  g_dht_raw[5];   /* 最近一次成功的原始 5 字节 [湿整][湿小][温整][温小][校验] */
extern volatile uint8_t  g_dht_ok;       /* 最近一次是否成功 */
extern volatile uint16_t g_dht_ok_cnt;   /* 累计成功次数 */
extern volatile uint16_t g_dht_fail_cnt; /* 累计失败次数 */

#ifdef __cplusplus
}
#endif

#endif /* BSP_DHT11_H */