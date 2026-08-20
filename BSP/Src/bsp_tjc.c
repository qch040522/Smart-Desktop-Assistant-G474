/**
 * @file    bsp_tjc.c
 * @brief   淘晶驰串口屏驱动实现。
 *
 *  自定义事件帧（屏幕工程内用 printh/crcrest/crcputh/crcval 组装发送）:
 *    [0x55][0x0D][cmd][p1_0..p1_3][p2_0..p2_3][p3_0..p3_3][CRC_H][CRC_L][0xAA]
 *   - 长度字段 = 0x0D (13) = cmd(1字节) + 3×4字节
 *   - CRC16-CCITT(init 0xFFFF, poly 0x1021) 覆盖 [长度字节 .. payload]
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "bsp.h"
#include "bsp_tjc.h"
#include "bsp_uart_link.h"     /* 复用 BspUartLink_Crc16 */

#define TJC_TX_TIMEOUT_MS   200u

/* ==================== 接收解析 ==================== */
typedef enum {
  TJ_SYNC,      /* 等待 0x55 */
  TJ_LEN,       /* 已收长度字节 */
  TJ_DATA,      /* 收集 payload */
  TJ_CRC_H,
  TJ_CRC_L
} tjc_rx_state_t;

typedef struct {
  tjc_rx_state_t state;
  uint16_t       crc;
  uint16_t       idx;
  uint16_t       len;
  uint8_t        buf[24u];
} tjc_rx_t;

static tjc_rx_t          s_rx;
static bsp_tjc_evt_cb_t  s_evt_cb;

/* 字节级 CRC16-CCITT 增量（同 esp-link 协议） */
static void crc_byte16(uint16_t *crc, uint8_t byte)
{
  uint8_t i;
  *crc ^= (uint16_t)byte << 8u;
  for (i = 0u; i < 8u; i++)
  {
    *crc = (*crc & 0x8000u) ? (uint16_t)((*crc << 1u) ^ 0x1021u) : (uint16_t)(*crc << 1u);
  }
}

static void rx_reset(void)
{
  s_rx.state = TJ_SYNC;
  s_rx.idx   = 0u;
  s_rx.len   = 0u;
  s_rx.crc   = 0xFFFFu;
}

static void emit_event(void)
{
  tjc_event_t evt;

  if (s_evt_cb == NULL) return;

  evt.cmd = s_rx.buf[0];
  evt.p1  = (int32_t)((uint32_t)s_rx.buf[1]  | ((uint32_t)s_rx.buf[2] << 8u) |
                       ((uint32_t)s_rx.buf[3]  << 16u) | ((uint32_t)s_rx.buf[4] << 24u));
  evt.p2  = (int32_t)((uint32_t)s_rx.buf[5]  | ((uint32_t)s_rx.buf[6] << 8u) |
                       ((uint32_t)s_rx.buf[7]  << 16u) | ((uint32_t)s_rx.buf[8] << 24u));
  evt.p3  = (int32_t)((uint32_t)s_rx.buf[9]  | ((uint32_t)s_rx.buf[10] << 8u) |
                       ((uint32_t)s_rx.buf[11] << 16u) | ((uint32_t)s_rx.buf[12] << 24u));
  s_evt_cb(&evt);
}

void BspTjc_RxByte(uint8_t byte)
{
  switch (s_rx.state)
  {
    case TJ_SYNC:
      if (byte == TJC_EVT_HEAD)
      {
        s_rx.state = TJ_LEN;
        s_rx.crc   = 0xFFFFu;
      }
      break;

    case TJ_LEN:
      s_rx.len = byte;
      crc_byte16(&s_rx.crc, byte);
      if (s_rx.len == TJC_PAYLOAD_LEN)
      {
        s_rx.idx = 0u;
        s_rx.state = TJ_DATA;
      }
      else
      {
        rx_reset();                  /* 长度不符 -> 丢弃 */
      }
      break;

    case TJ_DATA:
      s_rx.buf[s_rx.idx++] = byte;
      crc_byte16(&s_rx.crc, byte);
      if (s_rx.idx >= s_rx.len)
      {
        s_rx.state = TJ_CRC_H;
      }
      break;

    case TJ_CRC_H:
      if ((uint8_t)((s_rx.crc >> 8u) & 0xFFu) == byte)
      {
        s_rx.state = TJ_CRC_L;
      }
      else
      {
        rx_reset();
      }
      break;

    case TJ_CRC_L:
      if ((uint8_t)(s_rx.crc & 0xFFu) == byte)
      {
        emit_event();
      }
      rx_reset();
      break;

    default:
      rx_reset();
      break;
  }
}

/* ==================== 发送 ==================== */
static void tjc_send_bytes(const uint8_t *data, uint16_t len)
{
  HAL_UART_Transmit(&huart5, (uint8_t *)data, len, TJC_TX_TIMEOUT_MS);
}

void BspTjc_SendRaw(const char *cmd)
{
  uint8_t tail[3] = { 0xFFu, 0xFFu, 0xFFu };

  if (cmd == NULL) return;
  tjc_send_bytes((const uint8_t *)cmd, (uint16_t)strlen(cmd));
  tjc_send_bytes(tail, 3u);
}

void BspTjc_JumpPage(const char *page)
{
  char buf[64];
  (void)snprintf(buf, sizeof(buf), "page %s", page);
  BspTjc_SendRaw(buf);
}

void BspTjc_SetVal(const char *obj, int32_t val)
{
  char buf[64];
  (void)snprintf(buf, sizeof(buf), "%s=%ld", obj, (long)val);
  BspTjc_SendRaw(buf);
}

void BspTjc_SetText(const char *obj, const char *txt)
{
  char buf[96];
  (void)snprintf(buf, sizeof(buf), "%s=\"%s\"", obj, (txt != NULL) ? txt : "");
  BspTjc_SendRaw(buf);
}

/* ==================== 初始化 ==================== */
void BspTjc_Init(bsp_tjc_evt_cb_t cb)
{
  s_evt_cb = cb;
  rx_reset();

  /* UART5 逐字节中断接收（回调见 bsp_uart_cb.c） */
  HAL_UART_Receive_IT(&huart5, (uint8_t *)&g_u5_rx_byte, 1u);
}