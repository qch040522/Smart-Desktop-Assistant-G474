/**
 * @file    bsp_tjc.c
 * @brief   淘晶驰串口屏驱动实现。
 *
 *  事件帧（屏幕工程内用 printh/prints 组装发送, USART HMI 指令集版）:
 *    [0x55][0x0D][cmd][p1_0..p1_3][p2_0..p2_3][p3_0..p3_3][CRC_H][CRC_L]
 *   - 长度字段 = 0x0D (13) = cmd(1字节) + 3×4字节
 *   - 屏幕指令集版本不计算 CRC: 末尾 2 字节为占位(0x00 0x00), 本驱动跳过不校验
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "cmsis_os2.h"
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

/* 调试计数器(用 STM32 Programmer 内存读取确认链路):
 *   g_u5_rx_count  = UART5 收到的字节数(接线/波特率是否通)
 *   g_tjc_evt_count = 解析成功的事件数(帧格式是否正确)
 *   g_u5_dbg_buf   = 最近收到的原始字节环形缓冲(直接看屏幕发的内容) */
volatile uint32_t g_u5_rx_count   = 0u;
volatile uint32_t g_tjc_evt_count = 0u;
#define U5_DBG_BUF_LEN 128u
volatile uint8_t  g_u5_dbg_buf[U5_DBG_BUF_LEN];
volatile uint16_t g_u5_dbg_idx = 0u;

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
  g_tjc_evt_count++;                        /* 调试: 成功解析一帧事件 */

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
  g_u5_rx_count++;                          /* 调试: 每收到一个字节计数 */
  g_u5_dbg_buf[g_u5_dbg_idx % U5_DBG_BUF_LEN] = byte;   /* 调试: 存原始字节 */
  g_u5_dbg_idx++;

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
      /* 屏幕为指令集版本(USART HMI 指令集, 非LUA), 不计算 CRC;
         CRC 两字节为占位符(屏幕发送固定 0x00 0x00), 这里直接跳过不校验 */
      s_rx.state = TJ_CRC_L;
      break;

    case TJ_CRC_L:
      emit_event();
      rx_reset();
      break;

    default:
      rx_reset();
      break;
  }
}

/* ==================== 非阻塞发送(中断 + 环形队列) ====================
 * 原阻塞 HAL_UART_Transmit 超时 200ms/条, UI 每秒推多条时可能拖过秒边界
 * 导致 RTC 显示跳秒。改为 HAL_UART_Transmit_IT + 环形队列:
 *   - 调用方(任务)只入队立即返回, 不阻塞;
 *   - UART5 TXE 中断逐条发送, HAL_UART_TxCpltCallback 自动续发下一条。 */
#define TJC_TX_QUEUE      8u
#define TJC_TX_MAXLEN     100u

typedef struct {
  uint8_t  data[TJC_TX_MAXLEN];
  uint16_t len;
} tjc_tx_item_t;

static tjc_tx_item_t    s_tx_q[TJC_TX_QUEUE];
static volatile uint8_t s_tx_head = 0u;   /* 队头(发送中/待发送) */
static volatile uint8_t s_tx_tail = 0u;   /* 队尾(写入点) */
static volatile uint8_t s_tx_busy = 0u;   /* 正在 IT 发送 */

/* 取队头启动 IT 发送(任务/中断均可调用) */
static void tjc_tx_start_next(void)
{
  if (s_tx_head == s_tx_tail)
  {
    s_tx_busy = 0u;                       /* 队列空 */
    return;
  }
  if (HAL_UART_Transmit_IT(&huart5, s_tx_q[s_tx_head].data,
                           s_tx_q[s_tx_head].len) == HAL_OK)
  {
    s_tx_busy = 1u;
  }
  else
  {
    /* 启动失败(极少): 丢弃该条, 继续下一条 */
    s_tx_head = (uint8_t)((s_tx_head + 1u) % TJC_TX_QUEUE);
    tjc_tx_start_next();
  }
}

static void tjc_send_bytes(const uint8_t *data, uint16_t len)
{
  uint8_t next;
  if ((data == NULL) || (len == 0u) || (len > TJC_TX_MAXLEN)) return;

  /* 队列满: 等待空位(正常每秒仅几条, 不会满; 极端情况短暂等待) */
  next = (uint8_t)((s_tx_tail + 1u) % TJC_TX_QUEUE);
  while (next == s_tx_head)
  {
    osDelay(1u);
    next = (uint8_t)((s_tx_tail + 1u) % TJC_TX_QUEUE);
  }

  memcpy(s_tx_q[s_tx_tail].data, data, len);
  s_tx_q[s_tx_tail].len = len;
  s_tx_tail = next;

  if (!s_tx_busy)
  {
    tjc_tx_start_next();
  }
}

/* 发送完成回调入口(UART5 TxCplt, 中断上下文): 弹出队头并续发 */
void BspTjc_TxComplete(void)
{
  if (s_tx_head != s_tx_tail)
  {
    s_tx_head = (uint8_t)((s_tx_head + 1u) % TJC_TX_QUEUE);
  }
  tjc_tx_start_next();
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
  /* 淘晶驰数值赋值必须带 .val 属性, 如 page0.n0.val=123 */
  (void)snprintf(buf, sizeof(buf), "%s.val=%ld", obj, (long)val);
  BspTjc_SendRaw(buf);
}

void BspTjc_SetText(const char *obj, const char *txt)
{
  char buf[96];
  /* 淘晶驰文本赋值必须带 .txt 属性, 如 page0.t0.txt="abc" */
  (void)snprintf(buf, sizeof(buf), "%s.txt=\"%s\"", obj, (txt != NULL) ? txt : "");
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