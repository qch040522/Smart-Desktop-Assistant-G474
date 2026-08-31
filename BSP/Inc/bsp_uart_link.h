/**
 * @file    bsp_uart_link.h
 * @brief   ESP32 <-> STM32 UART 帧协议（需求 §10.1）。
 *
 *  帧格式: [0xAA][0x55][CMD][LEN_H][LEN_L][PAYLOAD...][CRC16_H][CRC16_L]
 *  - LEN 仅表示 PAYLOAD 字节数
 *  - CRC16-CCITT(init 0xFFFF, poly 0x1021)，覆盖范围:
 *      [LEN_H .. PAYLOAD]（自 LEN 起始，含 CMD 与 PAYLOAD；不含帧头）
 */
#ifndef BSP_UART_LINK_H
#define BSP_UART_LINK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 帧头 / 请求定义 */
#define BSP_LINK_HEAD0          0xAAu
#define BSP_LINK_HEAD1          0x55u
#define BSP_LINK_MAX_PAYLOAD    200u  /* 单帧允许最大 payload(对齐 ESP32 协议: 0x02 透传最长200) */

/* ==================== 命令表（对齐 ESP32 UART_PROTOCOL.md） ==================== */
/* ESP32 -> STM32 */
#define CMD_AI_STATE            0x01u   /* AI 状态: has_human,head_visible,pitch(i16 LE),keyp */
#define CMD_MQTT_CMD            0x02u   /* MQTT 指令(结构化二进制透传) */
#define CMD_CALIB_RESULT        0x03u   /* 校准完成: angle_base(i16,0.1°, 小端) */
#define CMD_TIME_SYNC           0x04u   /* 时间同步: unix_ts(u32 LE) + tz_offset(i16 LE) */

/* STM32 -> ESP32 */
#define CMD_TRIG_CALIB          0x10u   /* 触发坐姿校准(无payload) */
#define CMD_SET_REPORT_PERIOD   0x11u   /* u8 秒 */
#define CMD_DEV_STATUS          0x12u   /* 设备状态回传(16B, 多字节小端) */

/* ACK 回执(双向): CMD=0xA0, payload=[ack_cmd, status] (0成功/1失败/2忙/3参数错/4不支持) */
#define CMD_ACK                 0xA0u

/** 收到完整合法帧的回调 */
typedef void (*bsp_link_rx_cb_t)(uint8_t cmd, const uint8_t *payload, uint16_t len);

/** 初始化（注册收帧回调） */
void BspUartLink_Init(bsp_link_rx_cb_t cb);

/** 喂一个接收字节（在 USART3 RX 中断 / DMA 轮询中调用） */
void BspUartLink_RxByte(uint8_t byte);

/** 发送一帧（阻塞式，任务上下文中使用） */
void BspUartLink_SendFrame(uint8_t cmd, const uint8_t *payload, uint16_t len);

/** 发送 ACK（帧 CMD=0xA0, payload=[acked_cmd, status]） */
void BspUartLink_SendAck(uint8_t acked_cmd, uint8_t status);

/** CRC16-CCITT (查询用 / 供其它模块复用) */
uint16_t BspUartLink_Crc16(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* BSP_UART_LINK_H */