/**
 * @file    bsp_uart_link.c
 * @brief   ESP32 链路帧协议实现。
 */
#include "bsp.h"
#include "bsp_uart_link.h"

/* ==================== 解析状态机 ==================== */
#define BPL_MAX_PAYLOAD    64u

typedef enum {
  ST_SYNC1,   /* 等待 0xAA */
  ST_SYNC2,   /* 等待 0x55 */
  ST_CMD,     /* CMD(开始 CRC 计算) */
  ST_LEN_H,
  ST_LEN_L,
  ST_DATA,
  ST_CRC_H,
  ST_CRC_L
} rx_state_t;

typedef struct {
  rx_state_t state;
  uint16_t   crc;          /* 累计 CRC */
  uint8_t    cmd;
  uint16_t   len;
  uint16_t   idx;
  uint8_t    buf[BSP_LINK_MAX_PAYLOAD];
} link_parser_t;

static link_parser_t   s_p;
static bsp_link_rx_cb_t s_cb;

/* ESP32 链路已接收字节计数(调试用, 验证 RX 方向是否收到数据) */
volatile uint32_t g_uart3_rx_cnt = 0u;

/* ---------------------------------------------------------------- */
/* 字节级 CRC16-CCITT 增量更新 */
static void crc_byte(uint16_t *crc, uint8_t byte)
{
  uint8_t i;
  *crc ^= (uint16_t)byte << 8u;
  for (i = 0u; i < 8u; i++)
  {
    *crc = (*crc & 0x8000u) ? (uint16_t)((*crc << 1u) ^ 0x1021u) : (uint16_t)(*crc << 1u);
  }
}

uint16_t BspUartLink_Crc16(const uint8_t *data, uint16_t len)
{
  uint16_t crc = 0xFFFFu;
  while (len > 0u)
  {
    crc_byte(&crc, *data++);
    len--;
  }
  return crc;
}

/* ---------------------------------------------------------------- */
static void parser_reset(void)
{
  s_p.state = ST_SYNC1;
  s_p.idx   = 0u;
  s_p.len   = 0u;
  s_p.crc   = 0xFFFFu;
}

void BspUartLink_Init(bsp_link_rx_cb_t cb)
{
  s_cb = cb;
  parser_reset();
  /* 挂起首个 USART3 单字节接收(使能 RXNE 中断)。
   * 缺失该调用会导致 RXNEIE 未使能、ESP32 帧字节无法读取(ORRE 溢出)。
   * 之后每次接收完成由 HAL_UART_RxCpltCallback 自动重新挂起。 */
  HAL_UART_Receive_IT(&huart3, (uint8_t *)&g_u3_rx_byte, 1u);
}

/* ---------------------------------------------------------------- */
void BspUartLink_RxByte(uint8_t byte)
{
  g_uart3_rx_cnt++;          /* 每收一个字节计数(调试) */
  switch (s_p.state)
  {
    case ST_SYNC1:
      if (byte == BSP_LINK_HEAD0)
      {
        s_p.state = ST_SYNC2;
      }
      break;

    case ST_SYNC2:
      if (byte == BSP_LINK_HEAD1)
      {
        s_p.state = ST_CMD;
        s_p.crc   = 0xFFFFu;         /* CRC 自 CMD 起 */
        s_p.idx   = 0u;
      }
      else
      {
        s_p.state = (byte == BSP_LINK_HEAD0) ? ST_SYNC2 : ST_SYNC1;
      }
      break;

    case ST_CMD:
      s_p.cmd = byte;
      crc_byte(&s_p.crc, byte);
      s_p.state = ST_LEN_H;
      break;

    case ST_LEN_H:
      s_p.len = (uint16_t)byte << 8u;
      crc_byte(&s_p.crc, byte);
      s_p.state = ST_LEN_L;
      break;

    case ST_LEN_L:
      s_p.len |= (uint16_t)byte;
      crc_byte(&s_p.crc, byte);
      if (s_p.len == 0u)
      {
        s_p.state = ST_CRC_H;
      }
      else if (s_p.len > BSP_LINK_MAX_PAYLOAD)
      {
        parser_reset();                  /* 长度越界, 重新同步 */
      }
      else
      {
        s_p.state = ST_DATA;
      }
      break;

    case ST_DATA:
      s_p.buf[s_p.idx++] = byte;
      crc_byte(&s_p.crc, byte);
      if (s_p.idx >= s_p.len)
      {
        s_p.state = ST_CRC_H;
      }
      break;

    case ST_CRC_H:
      if ((uint8_t)((s_p.crc >> 8u) & 0xFFu) == byte)
      {
        s_p.state = ST_CRC_L;
      }
      else
      {
        parser_reset();
      }
      break;

    case ST_CRC_L:
      if ((uint8_t)(s_p.crc & 0xFFu) == byte)
      {
        if (s_cb != NULL)
        {
          s_cb(s_p.cmd, s_p.buf, s_p.len);
        }
      }
      parser_reset();
      break;

    default:
      parser_reset();
      break;
  }
}

/* ---------------------------------------------------------------- */
void BspUartLink_SendFrame(uint8_t cmd, const uint8_t *payload, uint16_t len)
{
  uint8_t  hdr[5];
  uint16_t crc;
  uint8_t  tail[2];

  hdr[0] = BSP_LINK_HEAD0;
  hdr[1] = BSP_LINK_HEAD1;
  hdr[2] = cmd;
  hdr[3] = (uint8_t)((len >> 8u) & 0xFFu);
  hdr[4] = (uint8_t)(len & 0xFFu);

  /* CRC 覆盖: 自 LEN_H 起, 含 CMD 与 PAYLOAD */
  crc = 0xFFFFu;
  crc_byte(&crc, cmd);
  crc_byte(&crc, hdr[3]);
  crc_byte(&crc, hdr[4]);
  {
    uint16_t i;
    for (i = 0u; i < len; i++)
    {
      crc_byte(&crc, payload ? payload[i] : 0u);
    }
  }
  tail[0] = (uint8_t)((crc >> 8u) & 0xFFu);
  tail[1] = (uint8_t)(crc & 0xFFu);

  HAL_UART_Transmit(&huart3, hdr, 5u, 50u);
  if (len > 0u)
  {
    HAL_UART_Transmit(&huart3, (uint8_t *)payload, len, 50u);
  }
  HAL_UART_Transmit(&huart3, tail, 2u, 50u);
}

/* ---------------------------------------------------------------- */
void BspUartLink_SendAck(uint8_t acked_cmd, uint8_t status)
{
  uint8_t payload[2];
  payload[0] = acked_cmd;   /* 被应答的 CMD 码 */
  payload[1] = status;      /* 0=成功 1=失败 2=忙 3=参数错误 4=不支持 */
  BspUartLink_SendFrame(CMD_ACK, payload, 2u);
}