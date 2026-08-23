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
static occupy_state_t s_occupy;
static link_state_t   s_link;
static uint8_t        s_threshold_lvl;
static uint8_t        s_unhuman_count;
static uint8_t        s_link_count;

void SvcState_Init(app_config_t *cfg)
{
  s_mode           = (sys_mode_t)cfg->sys_mode;
  if (s_mode >= SYS_MODE_NUM) s_mode = SYS_MODE_LEISURE;
  s_occupy         = OCCUPY_UNKNOWN;
  s_link           = LINK_OK;
  s_threshold_lvl  = cfg->angle_threshold;
  if (s_threshold_lvl >= POSTURE_TH_LVL_COUNT) s_threshold_lvl = POSTURE_TH_LVL_DEFAULT;
  s_unhuman_count  = 0u;
  s_link_count     = 0u;
}

/* 长期无效/should转休眠的判定 */
static int should_sleep(sys_mode_t m)
{
  /* 学习/休闲 才可能无人自动休眠 */
  return (m == SYS_MODE_STUDY) || (m == SYS_MODE_LEISURE);
}

void SvcState_Tick(uint32_t now_ms, uint8_t any_frame, uint8_t valid_frame)
{
  (void)now_ms;

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

  /* 无人判定: 连续>=3周期无有效关键点帧 */
  s_unhuman_count = valid_frame ? 0u : (uint8_t)(s_unhuman_count + 1u);
  if (s_unhuman_count >= UNHUMAN_FRAME_TH)
  {
    s_occupy = OCCUPY_NOBODY;
  }
  else if (valid_frame)
  {
    s_occupy = OCCUPY_HUMAN;
  }

#if 0   /* 临时测试(串口屏调试): 禁用无人自动休眠, 便于手动控制风扇/灯 */
  /* 若处于业务模式且无人 -> 进入业务休眠; 有人 -> 唤醒 */
  if (s_mode != SYS_MODE_SLEEP)
  {
    if ((s_occupy == OCCUPY_NOBODY) && should_sleep(s_mode))
    {
      s_mode = SYS_MODE_SLEEP;
    }
  }
  else
  {
    /* 休眠中有人可唤醒 */
    if (s_occupy == OCCUPY_HUMAN)
    {
      /* 唤醒到休闲（默认） */
      s_mode = SYS_MODE_LEISURE;
    }
  }
#endif
}

sys_mode_t    SvcState_Mode(void)   { return s_mode; }
occupy_state_t SvcState_Occupy(void) { return s_occupy; }
link_state_t   SvcState_Link(void)   { return s_link; }
uint8_t        SvcState_ThresholdDeg(void) { return s_th_lvls[s_threshold_lvl]; }
int            SvcState_IsSleeping(void)   { return (s_mode == SYS_MODE_SLEEP); }

void SvcState_SetThresholdLvl(uint8_t lvl)
{
  if (lvl >= POSTURE_TH_LVL_COUNT) return;
  s_threshold_lvl = lvl;
}

void SvcState_SetMode(sys_mode_t m)
{
  if (m >= SYS_MODE_NUM) return;
  /* 直接进入, 无人判定仍由 Tick 维持 */
  s_mode = m;
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
    case CMD_SET_SYS_MODE:
      if ((uint8_t)cmd->p1 < SYS_MODE_NUM)
      {
        SvcState_SetMode((sys_mode_t)cmd->p1);
        cfg->sys_mode = (uint8_t)cmd->p1;
        request_config_save();
      }
      break;

    case CMD_WAKEUP:
      if (s_mode == SYS_MODE_SLEEP)
      {
        s_mode = SYS_MODE_LEISURE;
        cfg->sys_mode = SYS_MODE_LEISURE;
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

    /* ---- 环境控制: 0x02 切页面; 0x03/0x04 设设备(自动挡/手动挡) ---- */
    case CMD_SET_CTRL_MODE:   /* 页面切换: 0自动监测页/1手动控制页 */
      if ((cmd->p1 == CTRL_MODE_AUTO) || (cmd->p1 == CTRL_MODE_MANUAL))
      {
        SvcEnv_SetCtrlMode((ctrl_mode_t)cmd->p1);
        cfg->ctrl_mode = (uint8_t)cmd->p1;
        request_config_save();
      }
      break;

    case CMD_FAN_LEVEL:
      /* 设备按钮只存在于手动控制页: 收到即隐式切回手动页模式(兜底, 屏幕切页0x02未配时也能用) */
      SvcEnv_SetCtrlMode(CTRL_MODE_MANUAL);
      if (cmd->p1 < 0)
      {
        SvcEnv_SetFanAuto();    /* 手动页风扇自动挡: 按温度 */
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
      SvcEnv_SetCtrlMode(CTRL_MODE_MANUAL);   /* 同上: 设备按钮=手动页 */
      if (cmd->p1 < 0)
      {
        SvcEnv_SetLampAuto();   /* 手动页台灯自动挡: 按光照 */
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
      break;

    case CMD_ALARM_ENABLE:
      SvcTimer_AlarmEnable((uint8_t)cmd->p1);
      cfg->alarm_en = (uint8_t)cmd->p1;
      request_config_save();
      break;

    case CMD_ALARM_RESET:
      SvcTimer_AlarmSet(0u, 0u, ALARM_REPEAT_ONCE);
      SvcTimer_AlarmEnable(0u);
      cfg->alarm_hour    = 0u;
      cfg->alarm_min     = 0u;
      cfg->alarm_en      = 0u;
      cfg->alarm_repeat  = ALARM_REPEAT_ONCE;
      cfg->alarm_weekday = 1u;
      request_config_save();
      break;

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