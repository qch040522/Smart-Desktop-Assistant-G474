/**
 * @file    svc_link.c
 * @brief   ESP32 通讯服务实现。
 */
#include "svc_link.h"
#include "bsp.h"
#include "bsp_uart_link.h"
#include "bsp_rtc.h"
#include "app_rtos.h"
#include "svc_posture.h"
#include "svc_timer.h"

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
    case 0x07u: c.id = CMD_SET_RTC_TIME;   /* 校时: p[0]=时 p[1]=分 p[2]=秒 */
      c.p1 = (p && len > 0u) ? p[0] : 0;
      c.p2 = (p && len > 1u) ? p[1] : 0;
      c.p3 = (p && len > 2u) ? p[2] : 0;
      break;
    default: c.id = CMD_NONE; break;
  }

  if (c.id != CMD_NONE)
  {
    osMessageQueuePut(qCmd, &c, 0u, 0u);
  }
}

/* 待回执的 ACK 命令（中断上下文置位, uart 任务轮询发送, 需求 §10.1 双向 ACK） */
static volatile uint8_t s_pending_ack = 0u;

/* ==================== 0x04 时间同步 (NTP -> STM32 RTC) ==================== */
/* UNIX 秒(已含时区偏移=本地时间) -> 日历。year 为后两位(0~99), weekday 1=周一..7=周日。
 * 公历算法 (Howard Hinnant), 输入范围 1970~2099。 */
static void unix_to_datetime(uint32_t sec, uint8_t *year, uint8_t *month,
                             uint8_t *day, uint8_t *weekday,
                             uint8_t *hour, uint8_t *min, uint8_t *sec_out)
{
  uint32_t days = sec / 86400u;
  uint32_t rem  = sec % 86400u;
  uint32_t z    = days + 719468u;              /* 自 0000-03-01 起天数 */
  uint32_t era  = z / 146097u;
  uint32_t doe  = z - era * 146097u;           /* [0, 146096] */
  uint32_t yoe  = (doe - doe/1460u + doe/36524u - doe/146096u) / 365u;
  uint32_t y    = yoe + era * 400u;
  uint32_t doy  = doe - (365u*yoe + yoe/4u - yoe/100u);
  uint32_t mp   = (5u*doy + 2u) / 153u;
  uint32_t d    = doy - (153u*mp + 2u)/5u + 1u;
  uint32_t m;

  if (mp < 10u)      m = mp + 3u;
  else               m = mp - 9u;
  if (m <= 2u) y++;

  *year    = (uint8_t)(y % 100u);
  *month   = (uint8_t)m;
  *day     = (uint8_t)d;
  *weekday = (uint8_t)(((days + 3u) % 7u) + 1u);  /* 1970-01-01 = 周四 = 4 */
  *hour    = (uint8_t)(rem / 3600u);
  *min     = (uint8_t)((rem % 3600u) / 60u);
  *sec_out = (uint8_t)(rem % 60u);
}

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
        /* pitch_angle 为 int16 小端(ESP32 原生, 对齐 UART_PROTOCOL.md §3.1) */
        f.pitch_angle    = (int16_t)((uint16_t)payload[2] | ((uint16_t)payload[3] << 8u));
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
        /* angle_base 为 int16 小端(ESP32 原生, 对齐 UART_PROTOCOL.md §3.3) */
        int16_t base = (int16_t)((uint16_t)payload[0] | ((uint16_t)payload[1] << 8u));
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

    case CMD_TIME_SYNC:
      if (len >= 6u)
      {
        /* unix_ts(u32 LE) + tz_offset(i16 LE), 本地时间 = unix_ts + tz_offset */
        uint32_t unix_ts = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8u) |
                           ((uint32_t)payload[2] << 16u) | ((uint32_t)payload[3] << 24u);
        int16_t  tz      = (int16_t)((uint16_t)payload[4] | ((uint16_t)payload[5] << 8u));
        /* 保护: 拒绝 2000 年之前的时间(SNTP 未同步时 unix 可能为 0/极小值) */
        if (unix_ts > 946684800u)
        {
          uint32_t local = (uint32_t)((int64_t)unix_ts + (int64_t)tz);
          uint8_t y, mo, d, wd, h, mi, s;
          unix_to_datetime(local, &y, &mo, &d, &wd, &h, &mi, &s);
          (void)BspRtc_SetDateTime(y, mo, d, wd, h, mi, s);
        }
      }
      /* 周期帧, 无需 ACK(同 0x01) */
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
    BspUartLink_SendAck(ack, 0u);   /* 0=成功 */
  }
}

void SvcLink_SendStatus(const dev_status_t *st)
{
  uint8_t payload[16];
  uint32_t v;
  if (st == NULL) return;

  /* 0x12 设备状态回传（对齐 UART_PROTOCOL.md §4.3, 16 字节, 多字节整数小端） */
  payload[0] = st->sys_mode;                                  /* 模式 0学习/1休闲/2休眠 */
  payload[1] = (uint8_t)((uint32_t)st->fan_level * 100u / (FAN_LEVELS - 1u)); /* 风扇 0~100 */
  payload[2] = (uint8_t)(st->lamp_brightness & 0xFFu);        /* 台灯亮度 0~255 */

  v = st->study_total_sec;                                    /* 总学习时长(s), u32 LE */
  payload[3]  = (uint8_t)(v & 0xFFu);
  payload[4]  = (uint8_t)((v >> 8u) & 0xFFu);
  payload[5]  = (uint8_t)((v >> 16u) & 0xFFu);
  payload[6]  = (uint8_t)((v >> 24u) & 0xFFu);

  v = st->study_today_sec;                                    /* 今日学习时长(s), u32 LE */
  payload[7]  = (uint8_t)(v & 0xFFu);
  payload[8]  = (uint8_t)((v >> 8u) & 0xFFu);
  payload[9]  = (uint8_t)((v >> 16u) & 0xFFu);
  payload[10] = (uint8_t)((v >> 24u) & 0xFFu);

  v = st->study_cur_sec;                                      /* 本次学习时长(s), u32 LE */
  payload[11] = (uint8_t)(v & 0xFFu);
  payload[12] = (uint8_t)((v >> 8u) & 0xFFu);
  payload[13] = (uint8_t)((v >> 16u) & 0xFFu);
  payload[14] = (uint8_t)((v >> 24u) & 0xFFu);

  payload[15] = SvcTimer_AlarmRinging();                      /* 1=闹钟响铃中 */

  BspUartLink_SendFrame(CMD_DEV_STATUS, payload, 16u);
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