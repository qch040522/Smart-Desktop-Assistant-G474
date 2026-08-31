/**
 * @file    svc_state.c
 * @brief   状态机服务实现。
 */
#include "svc_state.h"
#include "svc_config.h"
#include "svc_env.h"
#include "svc_timer.h"
#include "svc_posture.h"
#include "svc_link.h"
#include "app_rtos.h"
#include "bsp.h"
#include "bsp_rtc.h"

/* ---- 调试: 记录最近台灯命令的原始 p1(排查异常255来源), 用 STM32 Programmer 读 ---- */
volatile int32_t  g_lamp_cmd_hist[16];
volatile uint16_t g_lamp_cmd_idx = 0u;

/* 告警阈值档 (10/15/20/25°) */
static const uint8_t s_th_lvls[POSTURE_TH_LVL_COUNT] = { 10u, 15u, 20u, 25u };

static sys_mode_t     s_mode;
static study_mode_t   s_study;          /* 学习/休闲子状态 */
static occupy_state_t s_occupy;
static link_state_t   s_link;
static uint8_t        s_threshold_lvl;
static uint32_t       s_last_valid_ms;   /* 最近一次有效帧时刻, 无人判定用时间窗口 */
static uint8_t        s_link_count;

void SvcState_Init(app_config_t *cfg)
{
  s_mode           = (sys_mode_t)cfg->sys_mode;
  if (s_mode >= SYS_MODE_NUM) s_mode = SYS_MODE_AUTO;   /* 默认自动 */
  s_study          = (study_mode_t)cfg->study_mode;
  if (s_study >= STUDY_MODE_NUM) s_study = STUDY_MODE_LEISURE;  /* 默认休闲 */
  s_occupy         = OCCUPY_UNKNOWN;
  s_link           = LINK_OK;
  s_threshold_lvl  = cfg->angle_threshold;
  if (s_threshold_lvl >= POSTURE_TH_LVL_COUNT) s_threshold_lvl = POSTURE_TH_LVL_DEFAULT;
  s_last_valid_ms  = 0u;
  s_link_count     = 0u;
}

void SvcState_Tick(uint32_t now_ms, uint8_t any_frame, uint8_t valid_frame)
{
  /* 断链判定: 连续>=10周期无任何 0x01 帧 */
  s_link_count = any_frame ? 0u : (uint8_t)(s_link_count + 1u);
  if (s_link_count >= LINK_DOWN_FRAME_TH)
  {
    s_link = LINK_DOWN;
  }
  else
  {
    if (any_frame) s_link = LINK_OK;
  }

  /* 无人判定: 时间窗口。有效帧刷新"最近有效时刻";
   * 距离最近有效帧超过 UNHUMAN_TIMEOUT_MS 才判无人(容错 ESP32 慢帧率) */
  if (valid_frame)
  {
    s_last_valid_ms = now_ms;
    s_occupy = OCCUPY_HUMAN;
  }
  else if ((now_ms - s_last_valid_ms) >= UNHUMAN_TIMEOUT_MS)
  {
    s_occupy = OCCUPY_NOBODY;
  }
}

sys_mode_t    SvcState_Mode(void)   { return s_mode; }
study_mode_t  SvcState_Study(void)  { return s_study; }
occupy_state_t SvcState_Occupy(void) { return s_occupy; }
link_state_t   SvcState_Link(void)   { return s_link; }
uint8_t        SvcState_ThresholdDeg(void) { return s_th_lvls[s_threshold_lvl]; }

void SvcState_SetThresholdLvl(uint8_t lvl)
{
  if (lvl >= POSTURE_TH_LVL_COUNT) return;
  s_threshold_lvl = lvl;
}

void SvcState_SetMode(sys_mode_t m)
{
  if (m >= SYS_MODE_NUM) return;
  s_mode = m;
}

void SvcState_SetStudyMode(study_mode_t m)
{
  if (m >= STUDY_MODE_NUM) return;
  s_study = m;
}

/* 请求配置落盘（低频配置变更后调用, 需求 §七） */
static void request_config_save(void)
{
  uint8_t req = 0u;
  if (qFlash != NULL)
  {
    osMessageQueuePut(qFlash, &req, 0u, 0u);
  }
}

/* 请求时长快照落盘（总时长清零/退出学习时调用） */
static void request_timing_save(void)
{
  uint8_t req = 1u;
  if (qFlash != NULL)
  {
    osMessageQueuePut(qFlash, &req, 0u, 0u);
  }
}

void SvcState_ApplyCmd(const app_cmd_t *cmd, app_config_t *cfg)
{
  if (cmd == NULL) return;

  switch (cmd->id)
  {
    case CMD_SET_SYS_MODE:   /* 顶层系统模式: 0=自动 1=手动 */
      if ((uint8_t)cmd->p1 < SYS_MODE_NUM)
      {
        SvcState_SetMode((sys_mode_t)cmd->p1);
        cfg->sys_mode = (uint8_t)cmd->p1;
        request_config_save();
      }
      break;

    case CMD_SET_CTRL_MODE:  /* 学习/休闲子状态: 0=休闲 1=学习 */
      if ((uint8_t)cmd->p1 < STUDY_MODE_NUM)
      {
        SvcState_SetStudyMode((study_mode_t)cmd->p1);
        cfg->study_mode = (uint8_t)cmd->p1;
        request_config_save();
      }
      break;

    case CMD_SET_THRESHOLD_LVL:
      if ((uint8_t)cmd->p1 < POSTURE_TH_LVL_COUNT)
      {
        SvcState_SetThresholdLvl((uint8_t)cmd->p1);
        cfg->angle_threshold = (uint8_t)cmd->p1;
        request_config_save();
      }
      break;

    /* ---- 环境控制: 0x03/0x04 设设备(手动值) ---- */
    case CMD_FAN_LEVEL:
      /* 收到手动风扇指令: 隐式切到手动顶层模式(用户想手动控制) */
      SvcState_SetMode(SYS_MODE_MANUAL);
      cfg->sys_mode = SYS_MODE_MANUAL;
      if (cmd->p1 < 0)
      {
        SvcEnv_SetFanAuto();    /* 手动模式风扇自动挡: 按温度 */
      }
      else if ((uint8_t)cmd->p1 < FAN_LEVELS)
      {
        SvcEnv_SetFanManual((uint8_t)cmd->p1);
        cfg->fan_level = (uint8_t)cmd->p1;
      }
      request_config_save();
      break;

    case CMD_LAMP_BRIGHT:
      g_lamp_cmd_hist[g_lamp_cmd_idx % 16u] = cmd->p1;   /* 调试: 记录原始p1 */
      g_lamp_cmd_idx++;
      SvcState_SetMode(SYS_MODE_MANUAL);   /* 同上: 手动控制灯 */
      cfg->sys_mode = SYS_MODE_MANUAL;
      if (cmd->p1 < 0)
      {
        SvcEnv_SetLampAuto();   /* 手动模式台灯自动挡: 按光照 */
      }
      else if ((uint8_t)cmd->p1 <= LAMP_PWM_MAX)
      {
        SvcEnv_SetLampManual((uint8_t)cmd->p1);
        cfg->lamp_brightness = (uint8_t)cmd->p1;
      }
      request_config_save();
      break;

    /* ---- 坐姿校准（需求 §五） ---- */
    case CMD_START_CALIB:
      SvcPosture_StartCalibration();
      SvcLink_TriggerCalibration();      /* 下发 0x10 给 ESP32 */
      break;

    /* ---- 番茄钟（全程手动, 需求 §七） ---- */
    case CMD_POMO_ENABLE:
      SvcTimer_PomoEnable((uint8_t)cmd->p1);
      cfg->pomodo_en = (uint8_t)cmd->p1;
      request_config_save();
      break;

    case CMD_POMO_SET_MIN:
      {
        int32_t min = cmd->p1;
        if (min < 0)               min = 0;                              /* 负数按0处理(关闭番茄钟) */
        if (min > POMO_MAX_MINUTES) min = 0;                             /* 超限(>1000) -> 关闭番茄钟 */
        SvcTimer_PomoSetMin((uint16_t)min);
        cfg->pomodoro_min = (uint16_t)min;
        if (min > 0)
        {
          /* 设置时长即视为要使用番茄钟: 自动打开开关(与UI状态同步), 可直接开始 */
          SvcTimer_PomoEnable(1u);
          cfg->pomodo_en = 1u;
        }
        else
        {
          /* 时长设0=关闭番茄钟: 同步关闭开关 */
          SvcTimer_PomoEnable(0u);
          cfg->pomodo_en = 0u;
        }
        request_config_save();
      }
      break;

    case CMD_POMO_START:  SvcTimer_PomoStart();  break;
    case CMD_POMO_PAUSE:  SvcTimer_PomoPause();  break;
    case CMD_POMO_RESET:  SvcTimer_PomoReset();  break;

    /* ---- 闹钟（全模式生效, 需求 §七） ---- */
    case CMD_ALARM_SET:
      {
        uint8_t hh = (uint8_t)cmd->p1;
        uint8_t mm = (uint8_t)cmd->p2;
        uint8_t rep = (uint8_t)cmd->p3;              /* 0一次/1每日/2每周几 */
        if (hh > 23u) hh = 0u;                        /* 非法时/分 -> 归零(如25点->00点) */
        if (mm > 59u) mm = 0u;
        if (rep > ALARM_REPEAT_WEEKLY) rep = ALARM_REPEAT_DAILY;
        SvcTimer_AlarmSet(hh, mm, rep);
        if (rep == ALARM_REPEAT_WEEKLY)
        {
          SvcTimer_AlarmClearWeekdaySet();  /* 严格模式: 选每周必须重新设星期才能开 */
        }
        else
        {
          SvcTimer_AlarmWeekdayErrSet(0u);  /* 选一次/每日: 清除 ERR */
        }
        cfg->alarm_hour   = hh;
        cfg->alarm_min    = mm;
        cfg->alarm_repeat = rep;
        request_config_save();
      }
      break;

    case CMD_ALARM_WEEKDAY:
      if (((uint8_t)cmd->p1 >= 1u) && ((uint8_t)cmd->p1 <= 7u))
      {
        SvcTimer_AlarmSetWeekday((uint8_t)cmd->p1);
        cfg->alarm_weekday = (uint8_t)cmd->p1;
        request_config_save();
      }
      else
      {
        /* 非法星期(0或>7): 置 ERR 标志(每周模式下 trep 保持 ERR) + 屏显提示 */
        SvcTimer_AlarmWeekdayErrSet(1u);
        ui_msg_t m = { UE_ALARM_WD_ERR, 0, 0, 0 };
        osMessageQueuePut(qUI, &m, 0u, 0u);
      }
      break;

    case CMD_ALARM_ENABLE:
      if (SvcTimer_AlarmEnable((uint8_t)cmd->p1) == 0)
      {
        cfg->alarm_en = (uint8_t)cmd->p1;
        request_config_save();
      }
      /* 每周未设星期被拒: 不更新使能, talstatus 保持 CLOSE */
      break;

    case CMD_ALARM_RESET:
      SvcTimer_AlarmSet(0u, 0u, ALARM_REPEAT_ONCE);
      SvcTimer_AlarmClearSet();   /* 重置: 屏显 NULL */
      SvcTimer_AlarmClearWeekdaySet();
      SvcTimer_AlarmWeekdayErrSet(0u);
      (void)SvcTimer_AlarmEnable(0u);
      cfg->alarm_hour    = 0u;
      cfg->alarm_min     = 0u;
      cfg->alarm_en      = 0u;
      cfg->alarm_repeat  = ALARM_REPEAT_ONCE;
      cfg->alarm_weekday = 1u;
      request_config_save();
      break;

    case CMD_BEEP_STOP:  SvcTimer_AlarmStopRing();  break;  /* 关闭本次闹铃(下次照常) */

    /* ---- 总时长重置（写 Flash 置 0, 需求 §七） ---- */
    case CMD_RESET_TOTAL_STUDY:
      SvcTimer_ResetTotal();
      request_timing_save();
      break;

    /* ---- 校时（RTC, p1=时 p2=分 p3=秒; 日期保持当前） ---- */
    case CMD_SET_RTC_TIME:
    {
      uint8_t y, mo, d, w, h, mi, s;
      BspRtc_GetDate(&y, &mo, &d, &w);
      BspRtc_GetTime(&h, &mi, &s);
      h  = (uint8_t)(((uint8_t)cmd->p1) % 24u);
      mi = (uint8_t)(((uint8_t)cmd->p2) % 60u);
      s  = (uint8_t)(((uint8_t)cmd->p3) % 60u);
      (void)BspRtc_SetDateTime(y, mo, d, w, h, mi, s);
      break;
    }

    default:
      break;
  }
}