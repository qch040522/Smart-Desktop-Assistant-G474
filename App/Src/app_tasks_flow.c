/**
 * @file    app_tasks_flow.c
 * @brief   App 任务层（二）: state/posture/ui 任务 + TJC 事件 + AI 帧标志。
 */
#include <string.h>
#include <stdio.h>
#include "cmsis_os2.h"
#include "app_tasks.h"
#include "app_rtos.h"
#include "svc_state.h"
#include "svc_posture.h"
#include "svc_env.h"
#include "svc_timer.h"
#include "svc_config.h"
#include "svc_link.h"
#include "bsp_oled.h"
#include "bsp_led.h"
#include "bsp_tjc.h"
#include "bsp_rtc.h"

/* ---- AI 帧周期标志（state / posture 共享） ---- */
volatile uint8_t g_frame_any   = 0u;
volatile uint8_t g_frame_valid = 0u;

/* UI 刷新脏标志 */
static uint8_t g_ui_dirty = 1u;

/* OLED 通知行（告警/校准等临时提示） */
static char     s_notice[21] = "";
static uint32_t s_notice_until = 0u;

volatile uint32_t g_ui_dbg_time = 0u;   /* 调试: UI 最近读到的 RTC 时间 (h<<16|m<<8|s) */

/* TJC 对象名（淘晶驰指令集不支持下划线, 全部用字母数字短名; 两页都推番茄钟） */
#define TJC_OBJ_ALERT      "page_auto.talert"
#define TJC_OBJ_POMO       "page_auto.tpomo"
#define TJC_OBJ_POMO_MAN   "page_manual.tpomo2"
#define TJC_OBJ_TODAY      "page_auto.ttoday"
#define TJC_OBJ_TOTAL      "page_auto.ttotal"
#define TJC_OBJ_ALARM      "page_auto.talarm"
#define TJC_OBJ_ALARM_REP  "page_auto.trep"

static void ui_put(ui_msg_t m)
{
  osMessageQueuePut(qUI, &m, 0u, 0u);
}

/* 请求时长快照落盘（进入休眠/退出学习/重置总时长, 需求 §七） */
static void request_timing_save(void)
{
  uint8_t req = 1u;
  if (qFlash != NULL)
  {
    osMessageQueuePut(qFlash, &req, 0u, 0u);
  }
}

/* 秒 -> "HH:MM:SS" */
static void tjc_hms(char *buf, size_t n, uint32_t sec)
{
  (void)snprintf(buf, n, "%02lu:%02lu:%02lu",
                 (unsigned long)(sec / 3600u),
                 (unsigned long)((sec % 3600u) / 60u),
                 (unsigned long)(sec % 60u));
}

static const char *tjc_alarm_rep_txt(void)
{
  switch (SvcTimer_AlarmRepeat())
  {
    case ALARM_REPEAT_ONCE:   return "ONCE";
    case ALARM_REPEAT_WEEKLY: return "WEEKLY";
    default:                  return "DAILY";
  }
}

/* ================================================================ */
/*  TJC 触摸事件 -> 统一命令                                          */
/* ================================================================ */
static void on_tjc_event(const tjc_event_t *evt)
{
  app_cmd_t c;
  if (evt == NULL) return;

  c.id  = CMD_NONE;
  c.src = CMD_SRC_TJC;
  c.p1  = evt->p1;
  c.p2  = evt->p2;
  c.p3  = evt->p3;

  switch (evt->cmd)
  {
    case 0x01u: c.id = CMD_SET_SYS_MODE;       break;
    case 0x02u: c.id = CMD_SET_CTRL_MODE;      break;
    case 0x03u: c.id = CMD_FAN_LEVEL;          break;
    case 0x04u: c.id = CMD_LAMP_BRIGHT;        break;
    case 0x05u: c.id = CMD_START_CALIB;        break;
    case 0x06u: c.id = CMD_POMO_START;         break;
    case 0x07u: c.id = CMD_POMO_PAUSE;         break;
    case 0x08u: c.id = CMD_POMO_RESET;         break;
    case 0x09u: c.id = CMD_ALARM_ENABLE;       break;
    case 0x0Au: c.id = CMD_ALARM_RESET;        break;
    case 0x0Bu: c.id = CMD_RESET_TOTAL_STUDY;  break;
    /* 扩展（需求 §9 菜单: 闹钟设置/番茄钟时长/多档阈值/番茄钟开关/任意唤醒） */
    case 0x0Cu: c.id = CMD_ALARM_SET;          break;
    case 0x0Du: c.id = CMD_POMO_SET_MIN;       break;
    case 0x0Eu: c.id = CMD_SET_THRESHOLD_LVL;  break;
    case 0x0Fu: c.id = CMD_POMO_ENABLE;        break;
    case 0x10u: c.id = CMD_WAKEUP;             break;
    case 0x11u: c.id = CMD_SET_RTC_TIME;       break;  /* 校时: p1=时 p2=分 p3=秒 */
    case 0x12u: c.id = CMD_ALARM_WEEKDAY;      break;  /* 闹钟星期: p1=1(周一)~7(周日) */
    default:    c.id = CMD_NONE;               break;
  }
  if (c.id != CMD_NONE)
  {
    osMessageQueuePut(qCmd, &c, 0u, 0u);
  }
}

void App_UiInit(void)
{
  BspTjc_Init(on_tjc_event);
  /* BspOled_Display 移到 initTask(调度器启动后执行) */
  g_ui_dirty = 1u;
}

/* ================================================================ */
/*  stateTask: 命令路由 + 状态机推进                                 */
/* ================================================================ */
void App_TaskState(void *arg)
{
  (void)arg;
  app_cmd_t c;
  sys_mode_t    last_mode = SYS_MODE_NUM;
  occupy_state_t last_occ = 0xFEu;
  link_state_t  last_link = 0xFEu;

  for (;;)
  {
    Wdg_Heartbeat(WDG_SLOT_STATE);

    while (osMessageQueueGet(qCmd, &c, NULL, 0u) == osOK)
    {
      SvcState_ApplyCmd(&c, &g_cfg);
    }

    {
      uint8_t any = (g_frame_any != 0u);
      uint8_t vld = (g_frame_valid != 0u);
      g_frame_any = 0u; g_frame_valid = 0u;
      SvcState_Tick(HAL_GetTick(), any, vld);

      if (SvcState_Mode() != last_mode)
      {
        sys_mode_t nm = SvcState_Mode();
        /* 模式迁移副作用（需求 §四/§七/§11.1） */
        if (nm == SYS_MODE_SLEEP)
        {
          SvcLink_SetReportPeriod(3u);   /* 休眠期降频保活(2~3s) */
          request_timing_save();         /* 退出学习/进入休眠快照时长 */
        }
        else if (last_mode == SYS_MODE_SLEEP)
        {
          SvcLink_SetReportPeriod(1u);   /* 唤醒后恢复 1s 上报 */
        }
        else if (last_mode == SYS_MODE_STUDY)
        {
          request_timing_save();         /* 退出学习模式快照 */
        }
        last_mode = nm;
        ui_msg_t m = { UE_MODE_CHANGED, (int16_t)nm, 0, 0 };
        ui_put(m);
        g_status.sys_mode = (uint8_t)nm;
      }

      if (SvcState_Occupy() != last_occ)
      {
        last_occ = SvcState_Occupy();
        ui_msg_t m = { UE_OCCUPY_CHG, (int16_t)last_occ, 0, 0 };
        ui_put(m);
      }

      if (SvcState_Link() != last_link)
      {
        last_link = SvcState_Link();
        ui_msg_t m = { (last_link == LINK_DOWN) ? UE_LINK_DOWN : UE_LINK_UP, 0, 0, 0 };
        ui_put(m);
      }

      g_status.occupy     = (uint8_t)SvcState_Occupy();
      g_status.link_state = (uint8_t)SvcState_Link();
    }

    osDelay(1000u);
  }
}

/* ================================================================ */
/*  postureTask: 消费 AI 帧 -> 坐姿判定/校准                         */
/* ================================================================ */
void App_TaskPosture(void *arg)
{
  (void)arg;
  ai_frame_t f;

  for (;;)
  {
    Wdg_Heartbeat(WDG_SLOT_POSTURE);
    if (osMessageQueueGet(qAI, &f, NULL, 0u) == osOK)
    {
      g_frame_any = 1u;
      if (f.has_human && (f.keypoint_valid >= 4u))
      {
        g_frame_valid = 1u;
      }

      uint8_t thr = (uint8_t)(SvcState_IsSleeping() ? 0u : SvcState_ThresholdDeg());
      uint8_t trig = SvcPosture_OnAiFrame(&f, SvcState_Mode(), thr,
                     SvcPosture_GetBaseAngle(), HAL_GetTick());
      if (trig)
      {
        ui_msg_t m = { UE_POSTURE_ALARM, 0, 0, 0 };
        ui_put(m);
        uint8_t k;
        for (k = 0u; k < POSTURE_ALARM_FLASH_TIMES; k++)
        {
          BspLed_On();  osDelay(120u);
          BspLed_Off(); osDelay(120u);
        }
      }
    }
    else
    {
      osDelay(50u);
    }
  }
}

/* ================================================================ */
/*  uiTask: 消费事件 + 每秒刷新 OLED/TJC                              */
/* ================================================================ */
void App_TaskUi(void *arg)
{
  (void)arg;
  uint32_t last_refresh = 0u;

  for (;;)
  {
    Wdg_Heartbeat(WDG_SLOT_UI);

    ui_msg_t m;
    while (osMessageQueueGet(qUI, &m, NULL, 0u) == osOK)
    {
      g_ui_dirty = 1u;
      switch (m.id)
      {
        case UE_MODE_CHANGED:
          /* 模式变化: 由 OLED 状态行/屏幕告警体现, 屏幕不单独显示 */
          break;
        case UE_POSTURE_ALARM:
          (void)snprintf(s_notice, sizeof(s_notice), "POSTURE!");
          s_notice_until = BSP_GetTick() + 3000u;
          BspTjc_SetText(TJC_OBJ_ALERT, "POSTURE!");
          break;
        case UE_RECALIB_NEEDED:
          (void)snprintf(s_notice, sizeof(s_notice), "NEED RECALIB");
          s_notice_until = BSP_GetTick() + 5000u;
          BspTjc_SetText(TJC_OBJ_ALERT, "NEED RECALIB");
          break;
        case UE_CALIB_DONE:
          (void)snprintf(s_notice, sizeof(s_notice), "CALIB OK %d.%d", m.a / 10, m.a % 10);
          s_notice_until = BSP_GetTick() + 5000u;
          BspTjc_SetText(TJC_OBJ_ALERT, "CALIB OK");
          break;
        case UE_ALARM_RING:
          (void)snprintf(s_notice, sizeof(s_notice), "ALARM!");
          s_notice_until = BSP_GetTick() + 5000u;
          break;
        case UE_LINK_DOWN:
          (void)snprintf(s_notice, sizeof(s_notice), "LINK DOWN");
          s_notice_until = BSP_GetTick() + 5000u;
          break;
        case UE_LINK_UP:
          (void)snprintf(s_notice, sizeof(s_notice), "LINK OK");
          s_notice_until = BSP_GetTick() + 3000u;
          break;
        default:
          break;
      }
    }

    if (g_ui_dirty || ((BSP_GetTick() - last_refresh) >= 1000u))
    {
      last_refresh = BSP_GetTick(); g_ui_dirty = 0u;

      uint8_t h, mi, s, yr, mo, dy, wd;
      BspRtc_GetTime(&h, &mi, &s);
      BspRtc_GetDate(&yr, &mo, &dy, &wd);
      g_ui_dbg_time = ((uint32_t)h << 16) | ((uint32_t)mi << 8) | s;   /* 调试 */
      char line[21];
      BspOled_Clear();

      /* 中号(1.5x): 日期 + 时钟(居中) */
      (void)snprintf(line, sizeof(line), "%02d/%02d %02d:%02d:%02d", mo, dy, h, mi, s);
      BspOled_PutsMid(0u, (uint8_t)((16u - (uint8_t)strlen(line)) / 2u), line);

      {
        uint32_t pomo_rem = SvcTimer_PomoRemainSec();
        int len = (int)snprintf(line, sizeof(line), "Pomo %02d:%02d:%02d",
                               (int)(pomo_rem / 3600u),
                               (int)((pomo_rem % 3600u) / 60u),
                               (int)(pomo_rem % 60u));
        uint8_t col = (uint8_t)((16 - len) / 2u);   /* 中号每行16字符, 居中 */
        BspOled_PutsMid(2u, col, line);
      }

      /* 小字: 温湿度(~ 渲染为 ° -> ℃) / 光照 */
      (void)snprintf(line, sizeof(line), "T%d~C H%d%%", g_sensor.temp_x10 / 10,
                     (int)g_sensor.humi_pct);
      BspOled_Puts(4u, 0u, line);

      (void)snprintf(line, sizeof(line), "Lux %d", (int)g_sensor.lux);
      BspOled_Puts(5u, 0u, line);

      /* 单行状态/通知: 平时常驻链路状态, 有告警时临时显示告警 */
      if (BSP_GetTick() < s_notice_until)
      {
        BspOled_Puts(6u, 0u, s_notice);
      }
      else if (SvcState_Link() == LINK_DOWN)
      {
        BspOled_Puts(6u, 0u, "LINK DOWN");
      }
      else
      {
        BspOled_Puts(6u, 0u, "LINK OK");
      }

      BspOled_Flush();

      /* 周期推送关键状态到串口屏（page_auto, 需求 §9: 时分秒显示） */
      {
        char tbuf[24];

        tjc_hms(tbuf, sizeof(tbuf), SvcTimer_PomoRemainSec());
        BspTjc_SetText(TJC_OBJ_POMO, tbuf);      /* 自动页显示番茄钟 */
        BspTjc_SetText(TJC_OBJ_POMO_MAN, tbuf);   /* 手动页同步显示番茄钟 */

        tjc_hms(tbuf, sizeof(tbuf), SvcTimer_TodaySec());
        BspTjc_SetText(TJC_OBJ_TODAY, tbuf);

        tjc_hms(tbuf, sizeof(tbuf), SvcTimer_TotalSec());
        BspTjc_SetText(TJC_OBJ_TOTAL, tbuf);

        (void)snprintf(tbuf, sizeof(tbuf), "%02d:%02d",
                       (int)SvcTimer_AlarmHour(), (int)SvcTimer_AlarmMin());
        BspTjc_SetText(TJC_OBJ_ALARM, tbuf);
        BspTjc_SetText(TJC_OBJ_ALARM_REP, tjc_alarm_rep_txt());
      }
    }
    osDelay(200u);
  }
}