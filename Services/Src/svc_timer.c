/**
 * @file    svc_timer.c
 * @brief   计时服务实现。
 */
#include <stddef.h>
#include "svc_timer.h"
#include "bsp_rtc.h"
#include "bsp_buzzer.h"

typedef enum { POMO_IDLE = 0, POMO_RUN, POMO_PAUSE } pomo_state_e;

static uint32_t s_cur_sec = 0u;      /* 本次学习秒(会话内) */
static uint32_t s_today_sec = 0u;
static uint32_t s_total_sec = 0u;
static uint32_t s_last_ymd = 0u;     /* 上次跨天检查日期 */

static uint16_t s_pomo_total = POMO_DEFAULT_MINUTES; /* 分钟 */
static uint8_t  s_pomo_en = 1u;
static pomo_state_e s_pomo = POMO_IDLE;
static uint32_t s_pomo_remain = 0u;

static uint8_t  s_alarm_hh = 0u, s_alarm_mm = 0u, s_alarm_en = 0u;   /* 默认 0:00 */
static uint8_t  s_alarm_repeat = ALARM_REPEAT_ONCE;  /* 默认响一次 */
static uint8_t  s_alarm_weekday = 1u;                 /* 1=周一..7=周日 */
static uint8_t  s_alarm_ringing = 0u;
static uint32_t s_ring_start_ms = 0u;
/* 待生效闹钟设置(屏上"设置"先存缓冲, 点"开"才应用到实际并生效) */
static uint8_t  s_pend_hh = 0u, s_pend_mm = 0u;
static uint8_t  s_pend_repeat = ALARM_REPEAT_ONCE;
static uint8_t  s_pend_weekday = 1u;
static uint8_t  s_alarm_set = 0u;   /* 是否已设置闹钟时间 */
static uint8_t  s_weekday_set = 0u; /* 是否明确设置过星期(每周模式必须先设) */
static uint8_t  s_wd_err = 0u;      /* 星期输入非法标志(每周模式下保持 ERR) */static uint32_t s_pomo_ring_start_ms = 0u;            /* 番茄钟响铃起始(ms), 0=未响铃 */

void SvcTimer_Init(const app_timing_snapshot_t *snap)
{
  if (snap != NULL)
  {
    s_total_sec = snap->study_total_sec;
    s_today_sec = snap->study_today_sec;
    s_last_ymd  = snap->last_date_ymd;
  }
  else
  {
    s_total_sec = 0u;
    s_today_sec = 0u;
    s_last_ymd  = 0u;
  }

  if (s_last_ymd != BspRtc_GetYmd())
  {
    /* 跨天: 今日清零 */
    s_today_sec = 0u;
    s_last_ymd  = BspRtc_GetYmd();
  }
}

/* 按重复类型判断今天是否该响 */
static int alarm_day_match(void)
{
  uint8_t y, mo, d, w;

  if (s_alarm_repeat != ALARM_REPEAT_WEEKLY) return 1;  /* 响一次/每日 */
  BspRtc_GetDate(&y, &mo, &d, &w);
  return (w == s_alarm_weekday);                        /* 每周几匹配 */
}

void SvcTimer_Tick(study_mode_t study, uint32_t now_ms)
{
  uint32_t ymd = BspRtc_GetYmd();
  if (ymd != s_last_ymd)
  {
    s_today_sec = 0u;          /* RTC 跨天清零今日时长 */
    s_last_ymd = ymd;
  }

  /* 学习时长累加 */
  if (study == STUDY_MODE_STUDY)
  {
    s_cur_sec++;
    s_today_sec++;
    s_total_sec++;
  }
  else
  {
    s_cur_sec = 0u;            /* 退出学习重计 */
  }

  /* 番茄钟 */
  if ((s_pomo == POMO_RUN) && (s_pomo_remain > 0u))
  {
    s_pomo_remain--;
    if (s_pomo_remain == 0u)
    {
      /* 到点: 蜂鸣提示 3 秒后自动停 */
      s_pomo = POMO_IDLE;
      s_pomo_ring_start_ms = now_ms;
      BspBuzzer_BeepOn(4000u);
    }
  }
  else if ((s_pomo == POMO_RUN) && (s_pomo_remain == 0u))
  {
    /* 已结束, 保持空闲 */
    s_pomo = POMO_IDLE;
  }

  /* 番茄钟响铃: 超时自动停 */
  if ((s_pomo_ring_start_ms != 0u) &&
      ((now_ms - s_pomo_ring_start_ms) >= POMO_RING_MS))
  {
    s_pomo_ring_start_ms = 0u;
    BspBuzzer_BeepOff();
  }

  /* 闹钟: 检查时分匹配(按重复类型: 响一次/每日/每周几) */
  if (s_alarm_en && !s_alarm_ringing)
  {
    uint8_t h, m, s;
    BspRtc_GetTime(&h, &m, &s);
    if ((h == s_alarm_hh) && (m == s_alarm_mm) && (s < 5u) && alarm_day_match())
    {
      s_alarm_ringing = 1u;
      s_ring_start_ms = now_ms;
      BspBuzzer_BeepOn(3000u);
    }
  }
  if (s_alarm_ringing)
  {
    if ((now_ms - s_ring_start_ms) >= ALARM_RING_MS)
    {
      s_alarm_ringing = 0u;
      BspBuzzer_BeepOff();
      if (s_alarm_repeat == ALARM_REPEAT_ONCE)
      {
        s_alarm_en = 0u;      /* 响一次: 到点响完自动关闭使能 */
      }
    }
  }
}

/* ---- 时长 ---- */
uint32_t SvcTimer_CurSec(void)   { return s_cur_sec; }
uint32_t SvcTimer_TodaySec(void) { return s_today_sec; }
uint32_t SvcTimer_TotalSec(void) { return s_total_sec; }

void SvcTimer_ResetTotal(void)   { s_total_sec = 0u; }
/* ---- 番茄钟 ---- */
void SvcTimer_PomoSetMin(uint16_t min)
{
  if (min > POMO_MAX_MINUTES) min = 0u;  /* 超限(>1000)按0处理: 关闭番茄钟 */
  s_pomo_total = min;              /* 0 = 关闭番茄钟(开始无效, 见 PomoStart) */
  if (min == 0u)
  {
    s_pomo = POMO_IDLE;            /* 关闭: 停止当前计时并清空剩余 */
    s_pomo_remain = 0u;
    s_pomo_ring_start_ms = 0u;
    BspBuzzer_BeepOff();           /* 关闭时若在响铃则停止 */
  }
}

void SvcTimer_PomoSetSec(uint32_t sec)
{
  if (sec == 0u) sec = 1u;
  s_pomo_total = (uint16_t)((sec + 59u) / 60u);  /* 分钟参考(向上取整) */
  s_pomo_remain = sec;                            /* 精确剩余秒 */
  s_pomo = POMO_IDLE;
}

void SvcTimer_PomoEnable(uint8_t en)
{
  s_pomo_en = en ? 1u : 0u;
  if (!s_pomo_en)
  {
    /* 关闭开关: 停止计时并在响铃时停止蜂鸣 */
    s_pomo = POMO_IDLE;
    s_pomo_remain = 0u;
    s_pomo_ring_start_ms = 0u;
    BspBuzzer_BeepOff();
  }
}

void SvcTimer_PomoStart(void)
{
  if (!s_pomo_en) return;          /* 番茄钟开关关闭: 开始无效 */
  if (s_pomo_total == 0u) return;  /* 番茄钟已关闭(时长0): 开始无效 */
  if (s_pomo == POMO_IDLE)
  {
    s_pomo = POMO_RUN;
    if (s_pomo_remain == 0u)                       /* 未预设秒数则按整分钟启动 */
    {
      s_pomo_remain = (uint32_t)s_pomo_total * 60u;
    }
  }
  else if (s_pomo == POMO_PAUSE)
  {
    s_pomo = POMO_RUN;
  }
}

void SvcTimer_PomoPause(void)
{
  if (s_pomo == POMO_RUN) s_pomo = POMO_PAUSE;
}

void SvcTimer_PomoReset(void)
{
  s_pomo = POMO_IDLE;
  s_pomo_remain = 0u;
  s_pomo_ring_start_ms = 0u;
  BspBuzzer_BeepOff();             /* 手动重置: 若在响铃则停止 */
}

uint8_t  SvcTimer_PomoState(void)      { return (uint8_t)s_pomo; }
uint32_t SvcTimer_PomoRemainSec(void)  { return s_pomo_remain; }

/* ---- 闹钟 ---- */
/* 设置仅存入"待生效"缓冲, 点"开"(AlarmEnable(1)) 才应用到实际并开始生效 */
void SvcTimer_AlarmSet(uint8_t h, uint8_t m, uint8_t repeat)
{
  if (repeat > ALARM_REPEAT_WEEKLY) repeat = ALARM_REPEAT_DAILY;
  s_pend_hh = h;
  s_pend_mm = m;
  s_pend_repeat = repeat;
  s_alarm_set = 1u;   /* 设置过时间(含 0:00); 未设置/重置由 ClearSet 清除 */
}
void SvcTimer_AlarmClearSet(void)
{
  s_alarm_set = 0u;
}
void SvcTimer_AlarmSetWeekday(uint8_t wd)
{
  if ((wd >= 1u) && (wd <= 7u))
  {
    s_pend_weekday = wd;
    s_weekday_set  = 1u;   /* 明确设置过星期 */
    s_wd_err = 0u;         /* 正确星期: 清除 ERR */
  }
}
void SvcTimer_AlarmWeekdayErrSet(uint8_t on)
{
  s_wd_err = on ? 1u : 0u;
}
uint8_t SvcTimer_AlarmWeekdayErr(void)
{
  return s_wd_err;
}
void SvcTimer_AlarmClearWeekdaySet(void)
{
  s_weekday_set = 0u;
}
int SvcTimer_AlarmEnable(uint8_t en)
{
  if (en)
  {
    /* 每周模式必须先设星期, 否则拒绝开启 */
    if ((s_pend_repeat == ALARM_REPEAT_WEEKLY) && (s_weekday_set == 0u))
    {
      return -1;
    }
    /* 开启: 应用待生效设置 */
    s_alarm_hh      = s_pend_hh;
    s_alarm_mm      = s_pend_mm;
    s_alarm_repeat  = s_pend_repeat;
    s_alarm_weekday = s_pend_weekday;
  }
  s_alarm_en = en ? 1u : 0u;
  if (!s_alarm_en)
  {
    s_alarm_ringing = 0u;
    BspBuzzer_BeepOff();
  }
  return 0;
}
void SvcTimer_AlarmAck(void)
{
  s_alarm_ringing = 0u;
  if (s_alarm_repeat == ALARM_REPEAT_ONCE) s_alarm_en = 0u;  /* 响一次停后关闭 */
  BspBuzzer_BeepOff();
}
/* 关闭本次响铃(不改变使能/设置, 下次照常响) */
void SvcTimer_AlarmStopRing(void)
{
  s_alarm_ringing = 0u;
  BspBuzzer_BeepOff();
}
uint8_t SvcTimer_AlarmRinging(void) { return s_alarm_ringing; }
uint8_t SvcTimer_AlarmEnabled(void) { return s_alarm_en; }
uint8_t SvcTimer_AlarmHasSet(void)  { return s_alarm_set; }
/* 以下 getter 返回"待生效设置"值(供 UI/上报), 设置后立即反馈 */
uint8_t SvcTimer_AlarmHour(void)    { return s_pend_hh; }
uint8_t SvcTimer_AlarmMin(void)     { return s_pend_mm; }
uint8_t SvcTimer_AlarmRepeat(void)  { return s_pend_repeat; }
uint8_t SvcTimer_AlarmWeekday(void) { return s_pend_weekday; }

void SvcTimer_BuildSnapshot(app_timing_snapshot_t *snap)
{
  if (snap == NULL) return;
  snap->magic           = FLASH_TIM_MAGIC;
  snap->version         = 1u;
  snap->study_total_sec = s_total_sec;
  snap->study_today_sec = s_today_sec;
  snap->last_date_ymd   = BspRtc_GetYmd();
}