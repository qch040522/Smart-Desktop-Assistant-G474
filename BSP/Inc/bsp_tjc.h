/**
 * @file    bsp_tjc.h
 * @brief   淘晶驰 TJC 串口屏驱动（UART5 / PC12-TX, PD2-RX）。
 *
 *  - 发送: 淘晶驰「字符串指令 + 3×0xFF 帧尾」(官方被动解析协议)
 *      e.g.  page0.n0.val=100\xff\xff\xff
 *  - 接收: 自定义事件帧（屏幕工程内用 ppS/pTH 组装）:
 *      [0x55][len][cmd][p1(4,LE)][p2(4,LE)][p3(4,LE)][crc16_h][crc16_l]
 *    CRC16-CCITT 覆盖 [len..payload]
 */
#ifndef BSP_TJC_H
#define BSP_TJC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define TJC_EVT_HEAD   0x55u
#define TJC_EVT_TAIL   0xAAu
#define TJC_PAYLOAD_LEN  13u     /* len + cmd + 3×4B = 13 */

/** 屏幕事件（解析完成回调） */
typedef struct {
  uint8_t  cmd;
  int32_t  p1, p2, p3;
} tjc_event_t;

typedef void (*bsp_tjc_evt_cb_t)(const tjc_event_t *evt);

/** 初始化串口接收（启动单字节中断接收） */
void BspTjc_Init(bsp_tjc_evt_cb_t cb);

/** UART5 收到字节时喂入解析器（在中断/空闲中调用） */
void BspTjc_RxByte(uint8_t byte);

/** 发送原始字符串指令（自动追加 3×0xFF 帧尾） */
void BspTjc_SendRaw(const char *cmd);

/* —— 常用指令封装 —— */
void BspTjc_JumpPage(const char *page);
void BspTjc_SetVal(const char *obj, int32_t val);       /* obj = "page.n0.val" */
void BspTjc_SetText(const char *obj, const char *txt);  /* obj = "page.t0.txt" */

#ifdef __cplusplus
}
#endif

#endif /* BSP_TJC_H */