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

/* ==================== 告警状态机 ====================
 * 告警为"置位/清除"持久状态(非临时文本), OLED 第6行与 TJC talert 同步显示:
 *   POSTURE!     - 坐姿告警, 坐姿恢复正常后清除
 *   NEED RECALIB - 需重新校准, 校准完成后清除
 *   ALARM!       - 响铃, 响铃被关/自动停止后清除
 * 多个告警按发生先后入栈(最新在最前), 后发生覆盖先前;
 * 最新告警被处理后显示前一个, 全部清除后显示 NORMAL。
 * 注: 校准完成/星期非法/链路状态(LINK OK/DOWN)不再作为告警显示。 */
#define ALERT_POSTURE   0x01u
#define ALERT_RECALIB   0x02u
#define ALERT_RING      0x04u

static uint8_t s_alert_stack[3];   /* 活动告警栈, [0]=最新 */
static uint8_t s_alert_cnt = 0u;

static void alert_set(uint8_t flag)
{
  uint8_t i;
  for (i = 0u; i < s_alert_cnt; i++)
  {
    if (s_alert_stack[i] == flag)
    {
      for (; i > 0u; i--) s_alert_stack[i] = s_alert_stack[i - 1u];
      s_alert_stack[0] = flag;              /* 已存在: 刷新为最新 */
      return;
    }
  }
  for (i = s_alert_cnt; i > 0u; i--) s_alert_stack[i] = s_alert_stack[i - 1u];
  s_alert_stack[0] = flag;                  /* 新告警入栈顶(最新) */
  if (s_alert_cnt < 3u) s_alert_cnt++;
}

static void alert_clear(uint8_t flag)
{
  uint8_t i, j;
  for (i = 0u; i < s_alert_cnt; i++)
  {
    if (s_alert_stack[i] == flag)
    {
      for (j = i; (j + 1u) < s_alert_cnt; j++) s_alert_stack[j] = s_alert_stack[j + 1u];
      s_alert_cnt--;
      break;
    }
  }
}

static const char *alert_text(uint8_t flag)
{
  switch (flag)
  {
    case ALERT_POSTURE: return "POSTURE!";
    case ALERT_RECALIB: return "NEED RECALIB";
    case ALERT_RING:    return "ALARM!";
    default:            return "";
  }
}

/* ==================== TJC 推送去重缓存 ====================
 * BspTjc_SetText 已改为非阻塞(中断+队列), 但去重可进一步减少无效推送。 */
#define TJC_CACHE_SLOTS 11u
enum {
  TJC_SLOT_POMO = 0, TJC_SLOT_POMO_MAN, TJC_SLOT_TODAY, TJC_SLOT_TOTAL,
  TJC_SLOT_ALARM, TJC_SLOT_ALARM_MAN, TJC_SLOT_REP, TJC_SLOT_REP_MAN,
  TJC_SLOT_ST, TJC_SLOT_ST_MAN, TJC_SLOT_ALERT
};
static char    s_tjc_cache[TJC_CACHE_SLOTS][24];
static uint8_t s_tjc_dirty[TJC_CACHE_SLOTS];

volatile uint32_t g_ui_dbg_time = 0u;   /* 调试: UI 最近读到的 RTC 时间 (h<<16|m<<8|s) */

/* TJC 对象名（淘晶驰指令集不支持下划线, 全部用字母数字短名; 两页都推番茄钟） */
#define TJC_OBJ_ALERT      "page_auto.talert"
#define TJC_OBJ_POMO       "page_auto.tpomo"
#define TJC_OBJ_POMO_MAN   "page_manual.tpomo2"
#define TJC_OBJ_TODAY      "page_auto.ttoday"
#define TJC_OBJ_TOTAL      "page_auto.ttotal"
#define TJC_OBJ_ALARM      "page_auto.talarm"
#define TJC_OBJ_ALARM_MAN  "page_manual.talarm2"   /* 手动页闹钟时间 */
#define TJC_OBJ_ALARM_REP  "page_auto.trep"
#define TJC_OBJ_ALARM_REP_MAN "page_manual.trep2"  /* 手动页重复模式 */
#define TJC_OBJ_ALARM_ST   "page_auto.talstatus"   /* 闹钟开关状态 OPEN/CLOSE */
#define TJC_OBJ_ALARM_ST_MAN "page_manual.talstatus2" /* 手动页闹钟开关状态 */

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
  static const char *wd[] = { "MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN" };
  switch (SvcTimer_AlarmRepeat())
  {
    case ALARM_REPEAT_ONCE:   return "ONCE";
    case ALARM_REPEAT_WEEKLY:
    {
      uint8_t w = SvcTimer_AlarmWeekday();
      if ((w >= 1u) && (w <= 7u)) return wd[w - 1u];   /* WEEKLY 显示星期缩写 */
      return "WEEKLY";
    }
    default:                  return "DAILY";
  }
}

/* 推送去重: 仅值变化时发送(首次必发) */
static void tjc_push(uint8_t slot, const char *obj, const char *txt)
{
  if ((slot < TJC_CACHE_SLOTS) &&
      (s_tjc_dirty[slot] || (strcmp(s_tjc_cache[slot], txt) != 0)))
  {
    (void)strncpy(s_tjc_cache[slot], txt, sizeof(s_tjc_cache[slot]) - 1u);
    s_tjc_cache[slot][sizeof(s_tjc_cache[slot]) - 1u] = '\0';
    s_tjc_dirty[slot] = 0u;
    BspTjc_SetText(obj, txt);
  }
}

/* 更新 OLED 第6行 + TJC talert(告警闪烁 / NORMAL)。供主刷新与 500ms 闪烁刷新共用。
 * 自动清除: 坐姿恢复正常 / 响铃被关或自动停止; 校准完成在事件中清除 RECALIB。 */
static void update_alert_line(void)
{
  const char *alert_line = "";

  if (SvcPosture_AlarmActive() == 0u) alert_clear(ALERT_POSTURE);
  if (SvcTimer_AlarmRinging() == 0u)  alert_clear(ALERT_RING);

  if (s_alert_cnt > 0u)
  {
    alert_line = alert_text(s_alert_stack[0]);          /* 最新告警 */
    if ((BSP_GetTick() / 500u) & 1u)                    /* 持续闪烁: 500ms 亮/灭 */
    {
      BspOled_Puts(6u, 0u, alert_line);
      tjc_push(TJC_SLOT_ALERT, TJC_OBJ_ALERT, alert_line);
    }
    else
    {
      BspOled_Puts(6u, 0u, " ");
      tjc_push(TJC_SLOT_ALERT, TJC_OBJ_ALERT, " ");
    }
  }
  else
  {
    BspOled_Puts(6u, 0u, "NORMAL");
    tjc_push(TJC_SLOT_ALERT, TJC_OBJ_ALERT, "NORMAL");
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
    case 0x13u: c.id = CMD_BEEP_STOP;          break;  /* 关闭本次闹铃 */
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
  (void)memset(s_tjc_dirty, 1u, sizeof(s_tjc_dirty));   /* 首次必发 */
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
      /* keypoint_valid 为 0~3(鼻/左肩/右肩), ESP32 has_human 已含"有效点≥3且双肩有效"判定
       * (对齐 UART_PROTOCOL.md §3.1) */
      if (f.has_human && (f.keypoint_valid >= 3u))
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
        if (g_status.alert_flag != 0u)   /* 仍处于"需校准"状态: 告警闪烁后恢复常亮 */
        {
          BspLed_On();
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
  uint32_t last_refresh     = 0u;
  uint32_t last_alert_flash = 0u;

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
          alert_set(ALERT_POSTURE);
          break;
        case UE_RECALIB_NEEDED:
          alert_set(ALERT_RECALIB);
          BspLed_On();   /* MPU6050 挪动需校准: LED 常亮提示 */
          break;
        case UE_CALIB_DONE:
          alert_clear(ALERT_RECALIB);   /* 校准完成: 清除"需校准"告警(不显示 CALIB OK) */
          BspLed_Off();  /* 校准完成: 熄灭校准提示 LED */
          break;
        case UE_ALARM_RING:
          alert_set(ALERT_RING);
          break;
        default:
          break;
      }
    }

    /* 告警行独立 500ms 刷新(闪烁), 不等 1s 主刷新 */
    if ((BSP_GetTick() - last_alert_flash) >= 500u)
    {
      last_alert_flash = BSP_GetTick();
      update_alert_line();
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

      /* 单行状态/通知: 常驻 NORMAL / 告警(闪烁) */
      update_alert_line();

      BspOled_Flush();

      /* 周期推送关键状态到串口屏（page_auto, 需求 §9: 时分秒显示） */
      {
        char tbuf[24];

        tjc_hms(tbuf, sizeof(tbuf), SvcTimer_PomoRemainSec());
        tjc_push(TJC_SLOT_POMO, TJC_OBJ_POMO, tbuf);      /* 自动页显示番茄钟 */
        tjc_push(TJC_SLOT_POMO_MAN, TJC_OBJ_POMO_MAN, tbuf); /* 手动页同步显示番茄钟 */

        tjc_hms(tbuf, sizeof(tbuf), SvcTimer_TodaySec());
        tjc_push(TJC_SLOT_TODAY, TJC_OBJ_TODAY, tbuf);

        tjc_hms(tbuf, sizeof(tbuf), SvcTimer_TotalSec());
        tjc_push(TJC_SLOT_TOTAL, TJC_OBJ_TOTAL, tbuf);

        /* 闹钟时间: 未设置(0:00)/重置后显示 NULL */
        if (SvcTimer_AlarmHasSet())
        {
          (void)snprintf(tbuf, sizeof(tbuf), "%02d:%02d",
                         (int)SvcTimer_AlarmHour(), (int)SvcTimer_AlarmMin());
        }
        else
        {
          (void)snprintf(tbuf, sizeof(tbuf), "NULL");
        }
        tjc_push(TJC_SLOT_ALARM, TJC_OBJ_ALARM, tbuf);
        tjc_push(TJC_SLOT_ALARM_MAN, TJC_OBJ_ALARM_MAN, tbuf);
        if (!SvcTimer_AlarmHasSet())
        {
          tjc_push(TJC_SLOT_REP, TJC_OBJ_ALARM_REP, "NULL");   /* 未设置闹钟: NULL */
          tjc_push(TJC_SLOT_REP_MAN, TJC_OBJ_ALARM_REP_MAN, "NULL");
        }
        else if ((SvcTimer_AlarmRepeat() == ALARM_REPEAT_WEEKLY) && SvcTimer_AlarmWeekdayErr())
        {
          tjc_push(TJC_SLOT_REP, TJC_OBJ_ALARM_REP, "ERR");      /* 每周且星期非法: 保持 ERR */
          tjc_push(TJC_SLOT_REP_MAN, TJC_OBJ_ALARM_REP_MAN, "ERR");
        }
        else
        {
          tjc_push(TJC_SLOT_REP, TJC_OBJ_ALARM_REP, tjc_alarm_rep_txt());
          tjc_push(TJC_SLOT_REP_MAN, TJC_OBJ_ALARM_REP_MAN, tjc_alarm_rep_txt());
        }
        tjc_push(TJC_SLOT_ST, TJC_OBJ_ALARM_ST,
                 SvcTimer_AlarmEnabled() ? "OPEN" : "CLOSE");
        tjc_push(TJC_SLOT_ST_MAN, TJC_OBJ_ALARM_ST_MAN,
                 SvcTimer_AlarmEnabled() ? "OPEN" : "CLOSE");
      }
    }
    osDelay(200u);
  }
}