/**
 * @file    bsp_tjc.h
 * @brief   淘晶驰 TJC 串口屏驱动（UART5 / PC12-TX, PD2-RX）。
 *
 *  - 发送: 淘晶驰「字符串指令 + 3×0xFF 帧尾」(官方被动解析协议)
 *      e.g.  page_auto.t0.txt="abc"\xff\xff\xff
 *  - 接收: 事件帧（屏幕工程内用 printh/prints 组装, USART HMI 指令集版）:
 *      [0x55][len=0x0D][cmd][p1(4,LE)][p2(4,LE)][p3(4,LE)][crc_h][crc_l]
 *    CRC 两字节为占位符, 本驱动不校验(指令集屏幕无法便捷计算 CRC16-CCITT)。
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

/* 调试计数器(内存读取用) */
extern volatile uint32_t g_u5_rx_count;    /* UART5 收到的字节数 */
extern volatile uint32_t g_tjc_evt_count;  /* 解析成功的事件数 */
extern volatile uint8_t  g_u5_dbg_buf[128]; /* 原始字节环形缓冲 */
extern volatile uint16_t g_u5_dbg_idx;    /* 环形缓冲写入位置 */

/** UART5 收到字节时喂入解析器（在中断/空闲中调用） */
void BspTjc_RxByte(uint8_t byte);

/** UART5 TX 发送完成回调入口(中断上下文, 由 HAL_UART_TxCpltCallback 调用, 续发队列下一条) */
void BspTjc_TxComplete(void);

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