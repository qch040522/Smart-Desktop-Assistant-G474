/**
 * @file    svc_link.h
 * @brief   ESP32 通讯服务：解析 AI 帧入队、MQTT 命令入队、校准结果投递、
 *          发送设备状态/触发校准/设定上报周期。
 */
#ifndef SVC_LINK_H
#define SVC_LINK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_types.h"

/** 初始化（注册 UART 接收回调） */
void SvcLink_Init(void);

/** 周期性服务（uart 任务调用）: 发送挂起的 ACK 回执 */
void SvcLink_Service(void);

/** 在 App 循环中处理待上报状态（周期调用由 App 调度） */
void SvcLink_SendStatus(const dev_status_t *st);

/** 触发坐姿校准 (0x10) */
int  SvcLink_TriggerCalibration(void);

/** 设置 ESP32 MQTT/AI 上报周期秒级 (0x11) */
int  SvcLink_SetReportPeriod(uint8_t seconds);

/** MQTT 指令 0x02 结构化载荷解析（若无, 采用 JSON 文本方案占位） */
void SvcLink_HandleMqttPayload(const uint8_t *payload, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* SVC_LINK_H */