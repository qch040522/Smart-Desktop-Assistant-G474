/**
 * @file    svc_link.c
 * @brief   ESP32 通讯服务实现。
 */
#include "svc_link.h"
#include "bsp.h"
#include "bsp_uart_link.h"
#include "app_rtos.h"
#include "svc_posture.h"

/* 0x02 MQTT 指令载荷映射成 app_cmd（简化: 1字节命令+参数） */
static void mqtt_cmd_to_cmd(uint8_t cmd, const uint8_t *p, uint16_t len)
{
  app_cmd_t c;
  (void)len;
  c.id = CMD_NONE;
  c.src = CMD_SRC_ESP32;
  c.p1 = 0; c.p2 = 0; c.p3 = 0;

  switch (cmd)
  {
    case 0x01u: c.id = CMD_SET_SYS_MODE; c.p1 = (p && len) ? p[0] : 0; break;
    case 0x02u: c.id = CMD_SET_CTRL_MODE; c.p1 = (p && len) ? p[0] : 0; break;
    case 0x03u: c.id = CMD_FAN_LEVEL; c.p1 = (p && len) ? p[0] : 0; break;
    case 0x04u: c.id = CMD_LAMP_BRIGHT; c.p1 = (p && len) ? p[0] : 0; break;
    case 0x05u: c.id = CMD_START_CALIB; break;
    case 0x06u: c.id = CMD_WAKEUP; break;
    default: c.id = CMD_NONE; break;
  }

  if (c.id != CMD_NONE)
  {
    osMessageQueuePut(qCmd, &c, 0u, 0u);
  }
}

/* 待回执的 ACK 命令（中断上下文置位, uart 任务轮询发送, 需求 §10.1 双向 ACK） */
static volatile uint8_t s_pending_ack = 0u;

/* ==================== BSP 收帧回调 ==================== */
void svc_link_rx(uint8_t cmd, const uint8_t *payload, uint16_t len)
{
  switch (cmd)
  {
    case CMD_AI_STATE:
      if (len >= 5u)
      {
        ai_frame_t f;
        f.has_human      = payload[0];
        f.head_visible   = payload[1];
        f.pitch_angle    = (int16_t)(((uint16_t)payload[2] << 8u) | payload[3]);
        f.keypoint_valid = payload[4];
        f.tick_ms        = HAL_GetTick();
        if (qAI != NULL)
        {
          osMessageQueuePut(qAI, &f, 0u, 0u);
        }
      }
      /* AI 周期帧无需 ACK（避免高频回执） */
      break;

    case CMD_MQTT_CMD:
      if (len >= 1u)
      {
        mqtt_cmd_to_cmd(payload[0], (len > 1u) ? &payload[1] : NULL, len - 1u);
        s_pending_ack = CMD_MQTT_CMD;      /* 关键指令回执(任务轮询发送) */
      }
      break;

    case CMD_CALIB_RESULT:
      if (len >= 2u)
      {
        int16_t base = (int16_t)(((uint16_t)payload[0] << 8u) | payload[1]);
        SvcPosture_HandleCalibResult(base);

        /* 持久化基准角 + 清除"需重新校准"提醒（需求 §五） */
        g_cfg.angle_base = base;
        g_status.alert_flag = 0u;
        {
          uint8_t req = 0u;
          if (qFlash != NULL) osMessageQueuePut(qFlash, &req, 0u, 0u);
        }
        {
          ui_msg_t m = { UE_CALIB_DONE, (int16_t)base, 0, 0 };
          if (qUI != NULL) osMessageQueuePut(qUI, &m, 0u, 0u);
        }
        s_pending_ack = CMD_CALIB_RESULT;
      }
      break;

    default:
      break;
  }
}

void SvcLink_Init(void)
{
  BspUartLink_Init(svc_link_rx);
}

/* 周期性服务（uart 任务每 ~250ms 调用）: 发送挂起的 ACK */
void SvcLink_Service(void)
{
  if (s_pending_ack != 0u)
  {
    uint8_t ack = s_pending_ack;
    s_pending_ack = 0u;
    BspUartLink_SendAck(ack);
  }
}

void SvcLink_SendStatus(const dev_status_t *st)
{
  uint8_t payload[18];
  if (st == NULL) return;

  /* 0x12 设备状态回传（需求 §10.1: 模式/风扇档/台灯亮度/三种时长/闹钟状态）
   * 布局: [0..11] 保持早期约定, [12..17] 追加字段 */
  payload[0] = st->sys_mode;
  payload[1] = st->ctrl_mode;
  payload[2] = st->fan_level;
  payload[3] = (uint8_t)(st->lamp_brightness & 0xFFu);
  payload[4] = (uint8_t)((st->study_total_sec >> 24u) & 0xFFu);
  payload[5] = (uint8_t)((st->study_total_sec >> 16u) & 0xFFu);
  payload[6] = (uint8_t)((st->study_total_sec >> 8u)  & 0xFFu);
  payload[7] = (uint8_t)(st->study_total_sec & 0xFFu);
  payload[8] = st->pomo_state;
  payload[9] = (uint8_t)(st->pomo_remain_sec & 0xFFu);
  payload[10] = st->alarm_en;
  payload[11] = st->occupy;
  /* 追加: 今日/本次时长(u16, BE), 闹钟时/分 */
  payload[12] = (uint8_t)((st->study_today_sec >> 8u) & 0xFFu);
  payload[13] = (uint8_t)(st->study_today_sec & 0xFFu);
  payload[14] = (uint8_t)((st->study_cur_sec >> 8u) & 0xFFu);
  payload[15] = (uint8_t)(st->study_cur_sec & 0xFFu);
  payload[16] = st->alarm_hour;
  payload[17] = st->alarm_min;

  BspUartLink_SendFrame(CMD_DEV_STATUS, payload, 18u);
}

int SvcLink_TriggerCalibration(void)
{
  BspUartLink_SendFrame(CMD_TRIG_CALIB, NULL, 0u);
  return 0;
}

int SvcLink_SetReportPeriod(uint8_t seconds)
{
  BspUartLink_SendFrame(CMD_SET_REPORT_PERIOD, &seconds, 1u);
  return 0;
}

void SvcLink_HandleMqttPayload(const uint8_t *payload, uint16_t len)
{
  if ((payload != NULL) && (len > 0u))
  {
    /* 若采用文本 JSON, 在此解析; 当前已由 0x02 结构化处理 */
    svc_link_rx(CMD_MQTT_CMD, payload, len);
  }
}